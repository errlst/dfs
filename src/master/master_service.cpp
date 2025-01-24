#include "master_service.h"
#include "../common/listener.h"
#include "./global.h"
#include <set>

/* connection 需要使用的额外数据 */
enum conn_data : uint64_t {

    /* storage 数据 */
    storage_id,
    storage_ip,
    storage_port

};

struct {
    std::string ip;
    uint16_t port;
    uint16_t thread_count;
    std::chrono::milliseconds heartbeat_interval;
    std::chrono::milliseconds heartbeat_timeout;
    uint32_t master_magic = 0x12345678;
    uint16_t group_size = 3;
} static conf;

/* 多线程处理读消息 */
static auto ms_ios = std::vector<std::shared_ptr<asio::io_context>>{};
static auto ms_ios_guard = std::vector<asio::executor_work_guard<asio::io_context::executor_type>>{};

/* 注册的 storage <group, conn> */
static auto storage_conns = std::map<uint32_t, std::shared_ptr<connection_t>>{};

/* 计算分组 */
static auto calc_group(uint32_t id) -> uint32_t {
    return id % conf.group_size;
}

/* 获取组中的所有 storage */
static auto get_group_conns(uint32_t group) -> std::set<std::shared_ptr<connection_t>> {
    std::set<std::shared_ptr<connection_t>> conns;
    for (auto i = 0u, id = conf.group_size * group; i < conf.group_size; ++i, ++id) {
        if (storage_conns.contains(id)) {
            if (auto it = storage_conns.find(id); it != storage_conns.end()) {
                conns.insert(it->second);
            }
        }
    }
    return conns;
}

static auto init_conf() -> void {
    conf.ip = g_conf["master_service"]["ip"].get<std::string>();
    conf.port = g_conf["master_service"]["port"].get<std::uint16_t>();
    conf.thread_count = g_conf["master_service"]["thread_count"].get<std::uint16_t>();
    conf.heartbeat_interval = std::chrono::milliseconds{g_conf["network"]["heartbeat_interval"].get<std::uint32_t>()};
    conf.heartbeat_timeout = std::chrono::milliseconds{g_conf["network"]["heartbeat_timeout"].get<std::uint32_t>()};
}

static auto on_storage_disconnect(std::shared_ptr<connection_t> conn) -> asio::awaitable<void> {
    auto storage_id = conn->get_data<uint32_t>(conn_data::storage_id).value();
    auto group = calc_group(storage_id);
    storage_conns.erase(storage_id);
    g_log->log_info(std::format("storage {}:{} disconnect", conn->get_data<std::string>(conn_data::storage_ip).value(),
                                conn->get_data<uint16_t>(conn_data::storage_port).value()));

    /* board cast disconnect to storages in same group */
    for (auto conn : get_group_conns(group)) {
    }
    co_return;
}

using req_handle_t = std::function<asio::awaitable<void>(std::shared_ptr<connection_t>, std::shared_ptr<proto_frame_t>)>;

static auto sm_regist_handle(std::shared_ptr<connection_t> conn, std::shared_ptr<proto_frame_t> frame) -> asio::awaitable<void> {
    g_log->log_info(std::format("regist req from storage {}", conn->to_string()));
    if (frame->len < sizeof(sm_regist_req_t)) {
        g_log->log_error(std::format("invalid regist req from storage {}", conn->to_string()));
        co_await conn->send_res_frame(proto_frame_t{
            .cmd = (uint8_t)proto_cmd_e::sm_regist,
            .stat = 1,
        });
        co_return;
    }

    auto req_data = (sm_regist_req_t *)frame->data;
    if (req_data->master_magic != conf.master_magic) {
        g_log->log_error(std::format("invalid master magic from storage {}", conn->to_string()));
        co_await conn->send_res_frame(proto_frame_t{
            .cmd = (uint8_t)proto_cmd_e::sm_regist,
            .stat = 2,
        });
        co_return;
    }

    if (storage_conns.contains(req_data->storage_id)) {
        g_log->log_error(std::format("storage {} already regist", req_data->storage_id));
        co_await conn->send_res_frame(proto_frame_t{
            .cmd = (uint8_t)proto_cmd_e::sm_regist,
            .stat = 3,
        });
        co_return;
    }

    auto ip = std::string{req_data->ip, frame->len - sizeof(sm_regist_req_t)};
    conn->set_data(conn_data::storage_ip, std::move(ip));
    conn->set_data(conn_data::storage_port, req_data->port);
    co_await conn->send_res_frame(proto_frame_t{
        .cmd = (uint8_t)proto_cmd_e::sm_regist,
        .stat = 0,
    });
    conn->set_data(conn_data::storage_id, req_data->storage_id);
    g_log->log_info(std::format("storage {} regist suc", req_data->storage_id));

    /* board cast regist to storages in same group */
    for (auto conn : get_group_conns(calc_group(req_data->storage_id))) {
    }
    storage_conns[req_data->storage_id] = (conn);

    co_return;
}

static auto req_handles = std::map<uint8_t, req_handle_t>{
    {(uint8_t)proto_cmd_e::sm_regist, sm_regist_handle},
};

static auto read_from_connection(std::shared_ptr<connection_t> conn) -> asio::awaitable<void> {
    auto id = std::this_thread::get_id();
    while (true) {
        auto frame = co_await conn->recv_req_frame();
        if (!frame) {
            if (conn->get_data<uint32_t>(conn_data::storage_id).has_value()) {
                co_await on_storage_disconnect(conn);
            }
            co_return;
        }

        if (req_handles.contains(frame->cmd)) {
            co_await req_handles[frame->cmd](conn, frame);
        } else {
            g_log->log_error(std::format("unknown cmd {}", frame->cmd));
        }
    }
}

static auto work_as_master_service() -> asio::awaitable<void> {
    auto listener = listener_t{co_await asio::this_coro::executor,
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
    g_log->log_info(std::format("master service start on {}", listener.to_string()));

    for (auto i = 0; i < conf.thread_count; ++i) {
        ms_ios.push_back(std::make_shared<asio::io_context>());
        ms_ios_guard.push_back(asio::make_work_guard(*ms_ios[i]));
        std::thread{[i] {
            ms_ios[i]->run();
        }}.detach();
    }

    static auto idx = 0ull;
    while (true) {
        auto conn = co_await listener.accept();
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