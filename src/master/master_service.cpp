#include "master_service.h"
#include "../common/listener.h"
#include "./global.h"
#include <set>

/* connection 需要使用的额外数据 */
enum conn_data : uint64_t {
    /* storage 数据 */
    storage_id,
    storage_ip,
    storage_port,
    storage_magic,
};

struct {
    std::string ip;
    uint16_t port;
    uint16_t thread_count;
    uint32_t master_magic;
    uint16_t group_size;
    std::chrono::milliseconds heartbeat_interval;
    std::chrono::milliseconds heartbeat_timeout;
} static conf;

/* 多线程处理读消息 */
static auto ms_ios = std::vector<std::shared_ptr<asio::io_context>>{};
static auto ms_ios_guard = std::vector<asio::executor_work_guard<asio::io_context::executor_type>>{};

/* 注册的 storage <group, conn> */
static auto storage_conns = std::map<uint32_t, std::shared_ptr<connection_t>>{};

/* 计算分组，group 和 id 从 1 开始计数 */
static auto calc_group(uint32_t id) -> uint32_t {
    return (id - 1) / conf.group_size + 1;
}

/* 获取组中的所有 storage */
static auto get_group_conns(uint32_t group) -> std::set<std::shared_ptr<connection_t>> {
    std::set<std::shared_ptr<connection_t>> conns;
    for (auto i = 0u, id = conf.group_size * (group - 1) + 1; i < conf.group_size; ++i, ++id) {
        if (storage_conns.contains(id)) {
            if (auto it = storage_conns.find(id); it != storage_conns.end()) {
                conns.insert(it->second);
            }
        }
    }
    return conns;
}

static auto init_conf() -> void {
    conf = {
        .ip = g_conf["master_service"]["ip"].get<std::string>(),
        .port = g_conf["master_service"]["port"].get<std::uint16_t>(),
        .thread_count = g_conf["master_service"]["thread_count"].get<std::uint16_t>(),
        .master_magic = g_conf["master_service"]["master_magic"].get<std::uint32_t>(),
        .group_size = g_conf["master_service"]["group_size"].get<std::uint16_t>(),
        .heartbeat_interval = std::chrono::milliseconds{g_conf["network"]["heartbeat_interval"].get<std::uint32_t>()},
        .heartbeat_timeout = std::chrono::milliseconds{g_conf["network"]["heartbeat_timeout"].get<std::uint32_t>()},
    };
}

static auto on_storage_disconnect(std::shared_ptr<connection_t> conn) -> asio::awaitable<void> {
    auto storage_id = conn->get_data<uint32_t>(conn_data::storage_id).value();
    auto group = calc_group(storage_id);
    storage_conns.erase(storage_id);
    g_log->log_warn(std::format("storage {}:{} disconnect", conn->get_data<std::string>(conn_data::storage_ip).value(),
                                conn->get_data<uint16_t>(conn_data::storage_port).value()));

    /* board cast disconnect to storages in same group */
    for (auto conn : get_group_conns(group)) {
    }
    co_return;
}

using req_handle_t = std::function<asio::awaitable<void>(std::shared_ptr<connection_t>, std::shared_ptr<proto_frame_t>)>;

static auto sm_regist_handle(std::shared_ptr<connection_t> conn, std::shared_ptr<proto_frame_t> req_frame) -> asio::awaitable<void> {
    /* check frame valid */
    g_log->log_info(std::format("regist req from storage {}", conn->to_string()));
    if (req_frame->data_len < sizeof(sm_regist_req_t)) {
        g_log->log_error(std::format("invalid regist req from storage {}", conn->to_string()));
        co_await conn->send_res_frame(proto_frame_t{
            .cmd = (uint8_t)proto_cmd_e::sm_regist,
            .stat = 1,
        });
        co_return;
    }

    /* check frame data valid */
    auto req_data = (sm_regist_req_t *)req_frame->data;
    if (req_data->master_magic != conf.master_magic) {
        g_log->log_warn(std::format("invalid master magic from storage {} {} {}", conn->to_string(),
                                    conf.master_magic, req_data->master_magic));
        co_await conn->send_res_frame(proto_frame_t{
            .cmd = (uint8_t)proto_cmd_e::sm_regist,
            .stat = 2,
        });
        co_return;
    }

    if (storage_conns.contains(req_data->storage_id)) {
        g_log->log_warn(std::format("storage {} already regist", req_data->storage_id));
        co_await conn->send_res_frame(proto_frame_t{
            .cmd = (uint8_t)proto_cmd_e::sm_regist,
            .stat = 3,
        });
        co_return;
    }

    /* save storage info */
    conn->set_data(conn_data::storage_id, req_data->storage_id);
    conn->set_data(conn_data::storage_magic, req_data->storage_magic);
    conn->set_data(conn_data::storage_ip, conn->ip());
    conn->set_data(conn_data::storage_port, req_data->storage_port);
    g_log->log_info(std::format("storage {} regist suc", req_data->storage_id));

    /* response same group storage info */
    auto group_storages = get_group_conns(calc_group(req_data->storage_id));
    auto res_size = sizeof(sm_regist_res_t) * group_storages.size();
    for (auto storage : group_storages) {
        auto ip = storage->get_data<std::string>(conn_data::storage_ip).value();
        res_size += ip.size();
    }
    auto res_frame = (proto_frame_t *)malloc(sizeof(proto_frame_t) + res_size);
    *res_frame = {
        .id = req_frame->id,
        .cmd = req_frame->cmd,
        .data_len = (uint32_t)res_size,
    };
    auto p = (sm_regist_res_t *)res_frame->data;
    for (auto storage : group_storages) {
        auto ip = storage->get_data<std::string>(conn_data::storage_ip).value();
        *p = {
            .storage_magic = storage->get_data<uint16_t>(conn_data::storage_magic).value(),
            .storage_port = storage->get_data<uint16_t>(conn_data::storage_port).value(),
            .ip_len = (uint8_t)ip.size(),
        };
        memcpy(p->ip, ip.data(), ip.size());
        p = (sm_regist_res_t *)((char *)p + sizeof(sm_regist_res_t) + p->ip_len);
    }
    storage_conns[req_data->storage_id] = conn;

    co_await conn->send_res_frame(std::shared_ptr<proto_frame_t>{res_frame, [](auto p) { free(p); }});
    // co_await conn->send_res_frame(proto_frame_t{
    //     .cmd = (uint8_t)proto_cmd_e::sm_regist,
    //     .stat = 0,
    // });
}

static auto req_handles = std::map<uint8_t, req_handle_t>{
    {(uint8_t)proto_cmd_e::sm_regist, sm_regist_handle},
};

static auto read_from_connection(std::shared_ptr<connection_t> conn) -> asio::awaitable<void> {
    while (true) {
        auto frame = co_await conn->recv_req_frame();

        /* on disconnect */
        if (!frame) {
            if (conn->get_data<uint32_t>(conn_data::storage_id).has_value()) {
                co_await on_storage_disconnect(conn);
            }
            co_return;
        }

        /* call cmd handle */
        if (req_handles.contains(frame->cmd)) {
            co_await req_handles[frame->cmd](conn, frame);
        } else {
            g_log->log_error(std::format("unknown cmd {}", frame->cmd));
        }
    }
}

static auto work_as_master_service() -> asio::awaitable<void> {
    /* start accept */
    auto acceptor = listener_t{co_await asio::this_coro::executor,
                               listener_conf_t{
                                   .ip = conf.ip,
                                   .port = conf.port,
                                   .log = g_log,
                                   .conn_conf = {
                                       .heartbeat_timeout = conf.heartbeat_timeout,
                                       .heartbeat_interval = conf.heartbeat_interval,
                                       .log = g_log,
                                   },
                               }};
    g_log->log_info(std::format("master service start on {}", acceptor.to_string()));

    /* create conn io */
    for (auto i = 0; i < conf.thread_count; ++i) {
        ms_ios.push_back(std::make_shared<asio::io_context>());
        ms_ios_guard.push_back(asio::make_work_guard(*ms_ios[i]));
        std::thread{[i] {
            ms_ios[i]->run();
        }}.detach();
    }

    /* dispatch conn to io */
    static auto idx = 0ull;
    while (true) {
        auto conn = co_await acceptor.accept();
        if (conn) {
            auto io = ms_ios[idx++ % ms_ios.size()];
            asio::co_spawn(*io, conn->start(), asio::detached);
            asio::co_spawn(*io, read_from_connection(conn), asio::detached);
        }
    }
}

auto master_service() -> asio::awaitable<void> {
    g_log->log_info("master service start");
    init_conf();
    asio::co_spawn(co_await asio::this_coro::executor, work_as_master_service(), asio::detached);
    co_return;
}