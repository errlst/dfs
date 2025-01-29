#include "master_service.h"
#include "../common/acceptor.h"
#include "../common/util.h"
#include "../proto/proto.h"
#include "./global.h"
#include <set>

/* connection 需要使用的额外数据 */
enum conn_data : uint64_t {
    /* storage 数据 */
    s_id,
    s_ip,
    s_port,
    s_magic,
    s_free_disk,
};

struct {
    std::string ip;
    uint16_t port;
    uint16_t thread_count;
    uint32_t master_magic;
    uint16_t group_size;
    uint32_t heart_timeout;
    uint32_t heart_interval;
} static conf;

/* 多线程处理读消息 */
static auto ms_ios = std::vector<std::shared_ptr<asio::io_context>>{};
static auto ms_ios_guard = std::vector<asio::executor_work_guard<asio::io_context::executor_type>>{};

/*
    注册的 storage <id, conn>
    客户端请求 storage 时，进行循环赛，获取可用的 storage
    因此需要将 map 转换为 vector，方便进行轮询访问
    因此需要使用 mutex 保证转换时的数据安全
*/
static auto storage_conns = std::map<uint32_t, std::shared_ptr<connection_t>>{};
static auto storage_conns_vec = std::vector<std::shared_ptr<connection_t>>{};
static auto storage_conns_mut = std::mutex{};

/* 注册 storage */
static auto regist_storage(uint32_t id, std::shared_ptr<connection_t> conn) -> void {
    auto lock = std::unique_lock{storage_conns_mut};
    storage_conns[id] = conn;
    storage_conns_vec.emplace_back(conn);
}

/* 注销 storage */
static auto unregist_storage(uint32_t id) -> void {
    auto lock = std::unique_lock{storage_conns_mut};
    if (auto it = storage_conns.find(id); it != storage_conns.end()) {
        storage_conns.erase(it);
    }
    storage_conns_vec = {};
    for (const auto &[id, conn] : storage_conns) {
        storage_conns_vec.emplace_back(conn);
    }
}

/* 轮询获取下一个 connection */
static auto next_storage() -> std::shared_ptr<connection_t> {
    static auto idx = std::atomic_uint64_t{0};
    return storage_conns_vec[idx++ % storage_conns_vec.size()];
}

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
        .heart_timeout = g_conf["network"]["heart_timeout"].get<std::uint32_t>(),
        .heart_interval = g_conf["network"]["heart_interval"].get<std::uint32_t>(),
    };
}

using req_handle_t = std::function<asio::awaitable<void>(std::shared_ptr<connection_t>, std::shared_ptr<proto_frame_t>)>;

/*********************************************************************************************************************** */
/*********************************************************************************************************************** */
/* 服务 storage */

/* 定期获取 stroage 的可使用空间 */
static auto get_storage_usable_disk_impl(std::shared_ptr<connection_t> conn) -> asio::awaitable<bool> {
    auto id = co_await conn->send_req_frame(proto_frame_t{.cmd = (uint8_t)proto_cmd_e::ms_fs_free_size});
    if (!id) {
        co_return false;
    }

    auto res_frame = co_await conn->recv_res_frame(id.value());
    if (!res_frame) {
        co_return false;
    }

    auto useable_disk = *(uint64_t *)res_frame->data;
    // g_log->log_debug(std::format("storage {} max useable disk {}", conn->to_string(), useable_disk));
    conn->set_data(conn_data::s_free_disk, useable_disk);
    co_return true;
}

static auto get_storage_usable_disk(std::shared_ptr<connection_t> conn) -> asio::awaitable<void> {
    while (co_await get_storage_usable_disk_impl(conn)) {
        co_await co_sleep_for(std::chrono::seconds{5});
    }
}

/* storage 断连 */
static auto on_storage_disconnect(std::shared_ptr<connection_t> conn) -> asio::awaitable<void> {
    auto id = conn->get_data<uint32_t>(conn_data::s_id).value();
    unregist_storage(id);

    auto ip = conn->get_data<std::string>(conn_data::s_ip);
    auto port = conn->get_data<uint32_t>(conn_data::s_port);
    if (!ip.has_value() || !port.has_value()) {
        g_log->log_fatal("failed to get storage ip or port");
        exit(-1);
    }
    g_log->log_warn(std::format("storage {}:{} disconnect", ip.value(), port.value()));

    co_return;
}

static auto storage_req_handles = std::map<uint8_t, req_handle_t>{};

static auto read_from_storage(std::shared_ptr<connection_t> conn) -> asio::awaitable<void> {
    asio::co_spawn(co_await asio::this_coro::executor, get_storage_usable_disk(conn), asio::detached);
    while (true) {
        auto req_frame = co_await conn->recv_req_frame();
        if (!req_frame) {
            co_await on_storage_disconnect(conn);
            co_return;
        }

        if (storage_req_handles.contains(req_frame->cmd)) {
            co_await storage_req_handles[req_frame->cmd](conn, req_frame);
        } else {
            g_log->log_error(std::format("unknown storage cmd {}", req_frame->cmd));
        }
    }
}

static auto sm_regist_handle(std::shared_ptr<connection_t> conn, std::shared_ptr<proto_frame_t> req_frame) -> asio::awaitable<void> {
    g_log->log_info(std::format("regist req from storage {}", conn->to_string()));

    /* parse request data */
    auto req_data = dfs::proto::sm_regist::request_t{};
    if (!req_data.ParseFromArray(req_frame->data, req_frame->data_len)) {
        g_log->log_warn(std::format("failed to parse sm_regist req from storage {}", conn->to_string()));
        co_await conn->send_res_frame(
            proto_frame_t{
                .cmd = (uint8_t)proto_cmd_e::sm_regist,
                .stat = 1,
            },
            req_frame->id);
        co_return;
    }

    /* check master magic */
    if (req_data.master_magic() != conf.master_magic) {
        g_log->log_warn(std::format("invalid master magic from storage {} {}", conn->to_string(), req_data.master_magic()));
        co_await conn->send_res_frame(
            proto_frame_t{
                .cmd = (uint8_t)proto_cmd_e::sm_regist,
                .stat = 2,
            },
            req_frame->id);
        co_return;
    }

    /* check storage id */
    if (storage_conns.contains(req_data.storage_info().id())) {
        g_log->log_warn(std::format("storage {} already regist", req_data.storage_info().id()));
        co_await conn->send_res_frame(
            proto_frame_t{
                .cmd = (uint8_t)proto_cmd_e::sm_regist,
                .stat = 3,
            },
            req_frame->id);
        co_return;
    }

    /* response same group storage info */
    auto res_data = dfs::proto::sm_regist::response_t{};
    auto group = calc_group(req_data.storage_info().id());
    res_data.set_storage_group(group);
    for (auto storage : get_group_conns(group)) {
        auto info = res_data.add_storage_info();
        info->set_id(storage->get_data<uint32_t>(conn_data::s_id).value());
        info->set_port(storage->get_data<uint32_t>(conn_data::s_port).value());
        info->set_magic(storage->get_data<uint32_t>(conn_data::s_magic).value());
        info->set_ip(storage->get_data<std::string>(conn_data::s_ip).value());
    }
    auto res_frame = (proto_frame_t *)malloc(sizeof(proto_frame_t) + res_data.ByteSizeLong());
    *res_frame = {
        .cmd = (uint8_t)proto_cmd_e::sm_regist,
        .data_len = (uint32_t)res_data.ByteSizeLong(),
    };
    if (!res_data.SerializeToArray(res_frame->data, res_frame->data_len)) {
        g_log->log_error("failed to serialize sm_regist response");
        co_await conn->send_res_frame(
            proto_frame_t{
                .cmd = (uint8_t)proto_cmd_e::sm_regist,
                .stat = 4,
            },
            req_frame->id);
        co_return;
    }

    /* save storage info */
    conn->set_data(conn_data::s_id, req_data.storage_info().id());
    conn->set_data(conn_data::s_port, req_data.storage_info().port());
    conn->set_data(conn_data::s_magic, req_data.storage_info().magic());
    conn->set_data(conn_data::s_ip, req_data.storage_info().ip());
    regist_storage(req_data.storage_info().id(), conn);
    g_log->log_info(std::format("storage {} {} regist suc", req_data.storage_info().id(), req_data.storage_info().magic()));

    co_await conn->send_res_frame(std::shared_ptr<proto_frame_t>{res_frame, [](auto p) { free(p); }}, req_frame->id);
    asio::co_spawn(co_await asio::this_coro::executor, read_from_storage(conn), asio::detached);
}

/*********************************************************************************************************************** */
/*********************************************************************************************************************** */
/* 服务 client */

/* client 断连 */
static auto on_client_disconnect(std::shared_ptr<connection_t> conn) -> asio::awaitable<void> {
    g_log->log_warn(std::format("client {} disconnect", conn->to_string()));
    co_return;
}

static auto cm_valid_storage_handlle(std::shared_ptr<connection_t> conn, std::shared_ptr<proto_frame_t> req_frame) -> asio::awaitable<void> {
    /* get a valid storage */
    auto valid_storage = std::shared_ptr<connection_t>{};
    for (auto i = 0; i < storage_conns_vec.size(); ++i) {
        auto storage = next_storage();
        auto usable_disk = storage->get_data<uint64_t>(conn_data::s_free_disk).value();
        if (usable_disk > *(uint64_t *)req_frame->data) {
            valid_storage = storage;
            break;
        }
    }
    if (!valid_storage) {
        g_log->log_warn(std::format("not find valid storage for size {}", *(uint64_t *)req_frame->data));
        co_await conn->send_res_frame(
            proto_frame_t{
                .cmd = req_frame->cmd,
                .stat = 1,
            },
            req_frame->id);
        co_return;
    }

    /* response */
    auto res_data = dfs::proto::cm_valid_storage::response_t{};
    res_data.set_storage_magic(valid_storage->get_data<uint32_t>(conn_data::s_magic).value());
    res_data.set_storage_port(valid_storage->get_data<uint32_t>(conn_data::s_port).value());
    res_data.set_storage_ip(valid_storage->get_data<std::string>(conn_data::s_ip).value());
    auto res_frame = (proto_frame_t *)malloc(sizeof(proto_frame_t) + res_data.ByteSizeLong());
    *res_frame = {
        .id = req_frame->id,
        .cmd = req_frame->cmd,
        .data_len = (uint32_t)res_data.ByteSizeLong(),
    };
    if (!res_data.SerializeToArray(res_frame->data, res_frame->data_len)) {
        g_log->log_error("failed to serialize cm_valid_storage response");
        co_await conn->send_res_frame(
            proto_frame_t{
                .cmd = req_frame->cmd,
                .stat = 2,
            },
            req_frame->id);
        co_return;
    }
    co_await conn->send_res_frame(std::shared_ptr<proto_frame_t>{res_frame, [](auto p) { free(p); }}, req_frame->id);
}

static auto client_req_handles = std::map<uint8_t, req_handle_t>{
    {(uint8_t)proto_cmd_e::cm_valid_storage, cm_valid_storage_handlle},
};

static auto read_from_client(std::shared_ptr<connection_t> conn) -> asio::awaitable<void> {
    while (true) {
        auto req_frame = co_await conn->recv_req_frame();
        if (!req_frame) {
            co_await on_client_disconnect(conn);
            co_return;
        }

        /* storage regist if suc, storage conn will run in read_from_storag */
        if (req_frame->cmd == (uint8_t)proto_cmd_e::sm_regist) {
            co_await sm_regist_handle(conn, req_frame);
            co_return;
        }

        /* call cmd handle */
        if (client_req_handles.contains(req_frame->cmd)) {
            co_await client_req_handles[req_frame->cmd](conn, req_frame);
        } else {
            g_log->log_error(std::format("unknown client cmd {}", req_frame->cmd));
        }
    }
}

static auto work_as_master_service() -> asio::awaitable<void> {
    /* start accept */
    auto acceptor = acceptor_t{co_await asio::this_coro::executor, acceptor_conf_t{
                                                                       .ip = conf.ip,
                                                                       .port = conf.port,
                                                                       .heart_timeout = conf.heart_timeout,
                                                                       .heart_interval = conf.heart_interval,
                                                                       .log = g_log}};
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
        auto io = ms_ios[idx++ % ms_ios.size()];
        asio::co_spawn(*io, conn->start(), asio::detached);
        asio::co_spawn(*io, read_from_client(conn), asio::detached);
    }
}

auto master_service() -> asio::awaitable<void> {
    init_conf();
    asio::co_spawn(co_await asio::this_coro::executor, work_as_master_service(), asio::detached);
    co_return;
}