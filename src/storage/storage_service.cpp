#include "./storage_service.h"
#include "../common/acceptor.h"
#include "../common/util.h"
#include "../proto/proto.h"
#include "./global.h"
#include "./store.h"
#include <set>

enum conn_data : uint64_t {
    /* client 数据 */
    c_file_id,

};

struct {
    uint32_t id;
    std::string ip;
    uint16_t port;
    std::string master_ip;
    uint16_t master_port;
    uint16_t thread_count;
    uint16_t storage_magic;
    uint32_t master_magic;
    std::vector<std::string> hot_paths;
    std::vector<std::string> warm_paths;
    std::vector<std::string> cold_paths;
    uint32_t heart_timeout;
    uint32_t heart_interval;
} static conf;

static std::shared_ptr<store_ctx_group_t> hot_stores;
static std::shared_ptr<store_ctx_group_t> warm_stores;
static std::shared_ptr<store_ctx_group_t> cold_stores;

static auto ss_ios = std::vector<std::shared_ptr<asio::io_context>>{};
static auto ss_ios_guard = std::vector<asio::executor_work_guard<asio::io_context::executor_type>>{};

static auto master_conn = std::shared_ptr<connection_t>{};             // master 服务器连接
static auto storage_conns = std::set<std::shared_ptr<connection_t>>{}; // 同组 storage 服务器连接

static auto storage_group_id = (uint32_t)-1;

static auto init_conf() -> void {
    conf = {
        .id = g_conf["storage_service"]["id"].get<uint32_t>(),
        .ip = g_conf["storage_service"]["ip"].get<std::string>(),
        .port = g_conf["storage_service"]["port"].get<uint16_t>(),
        .master_ip = g_conf["storage_service"]["master_ip"].get<std::string>(),
        .master_port = g_conf["storage_service"]["master_port"].get<uint16_t>(),
        .thread_count = g_conf["storage_service"]["thread_count"].get<uint16_t>(),
        .storage_magic = (uint16_t)std::random_device{}(),
        .master_magic = g_conf["storage_service"]["master_magic"].get<uint32_t>(),
        .hot_paths = g_conf["storage_service"]["hot_paths"].get<std::vector<std::string>>(),
        .warm_paths = g_conf["storage_service"]["warm_paths"].get<std::vector<std::string>>(),
        .cold_paths = g_conf["storage_service"]["cold_paths"].get<std::vector<std::string>>(),
        .heart_timeout = g_conf["network"]["heart_timeout"].get<uint32_t>(),
        .heart_interval = g_conf["network"]["heart_interval"].get<uint32_t>(),
    };
}

static auto init_stores() -> void {
    hot_stores = std::make_shared<store_ctx_group_t>("hot_store_group", conf.hot_paths);
    warm_stores = std::make_shared<store_ctx_group_t>("warm_store_group", conf.warm_paths);
    cold_stores = std::make_shared<store_ctx_group_t>("cold_store_group", conf.cold_paths);
}

using req_handle_t = std::function<asio::awaitable<void>(std::shared_ptr<connection_t>, std::shared_ptr<proto_frame_t>)>;

/************************************************************************************************************* */
/************************************************************************************************************* */
/* 处理 storage 消息 */

static auto on_storage_disconnect(std::shared_ptr<connection_t> conn) -> asio::awaitable<void> {
    g_log->log_warn(std::format("storage {} disconnect", conn->to_string()));
    storage_conns.erase(conn);
    co_return;
}

static auto storage_req_handles = std::map<proto_cmd_e, req_handle_t>{};

static auto request_from_storage(std::shared_ptr<connection_t> conn) -> asio::awaitable<void> {
    while (true) {
        auto req_frame = co_await conn->recv_req_frame();
        if (!req_frame) {
            co_return co_await on_storage_disconnect(conn);
        }

        auto handle = storage_req_handles.find((proto_cmd_e)req_frame->cmd);
        if (handle == storage_req_handles.end()) {
            g_log->log_warn(std::format("unhandle cmd {} from storage {}", req_frame->cmd, conn->to_string()));
            continue;
        }
        co_await handle->second(conn, req_frame);
    }

    co_return;
}

/************************************************************************************************************* */
/************************************************************************************************************* */
/* 处理 client 消息 */

static auto on_client_disconnect(std::shared_ptr<connection_t> conn) -> asio::awaitable<void> {
    g_log->log_info(std::format("client {} disconnect", conn->to_string()));
    co_return;
}

static auto ss_regist_handle(std::shared_ptr<connection_t> conn, std::shared_ptr<proto_frame_t> req_frame) -> asio::awaitable<void> {
    if (req_frame->data_len != sizeof(uint32_t)) {
        g_log->log_error(std::format("invalid ss_regist data_len {}", (uint32_t)req_frame->data_len));
        co_return;
    }

    if (*(uint32_t *)req_frame->data != conf.storage_magic) {
        g_log->log_error(std::format("invalid ss_regist magic {} {}", conf.storage_magic, *(uint32_t *)req_frame->data));
        co_return;
    }

    /* regist suc */
    if (!co_await conn->send_res_frame(
            proto_frame_t{
                .cmd = (uint8_t)proto_cmd_e::ss_regist,
            },
            req_frame->id)) {
        co_return co_await on_client_disconnect(conn);
    }
    g_log->log_info(std::format("storage {} regist suec", conn->to_string()));
    storage_conns.insert(conn);
    asio::co_spawn(co_await asio::this_coro::executor, request_from_storage(conn), asio::detached);
}

static auto cs_create_file_handle(std::shared_ptr<connection_t> conn, std::shared_ptr<proto_frame_t> req_frame) -> asio::awaitable<void> {
    if (conn->has_data(conn_data::c_file_id)) {
        g_log->log_error(std::format("file already created {}", conn->to_string()));
        co_await conn->send_res_frame(
            proto_frame_t{
                .cmd = (uint8_t)proto_cmd_e::cs_create_file,
                .stat = 1,
            },
            req_frame->id);
        co_return;
    }

    if (req_frame->data_len != sizeof(uint64_t)) {
        g_log->log_error(std::format("invalid cs_create_file data_len {}", (uint32_t)req_frame->data_len));
        co_return;
    }

    /* create fille */
    auto file_id = hot_stores->create_file(*(uint64_t *)req_frame->data);
    if (!file_id) {
        g_log->log_error("failed to create file");
        co_await conn->send_res_frame(
            proto_frame_t{
                .cmd = (uint8_t)proto_cmd_e::cs_create_file,
                .stat = 2,
            },
            req_frame->id);
        co_return;
    }
    conn->set_data(conn_data::c_file_id, file_id.value());

    /* response */
    co_await conn->send_res_frame(
        proto_frame_t{
            .cmd = (uint8_t)proto_cmd_e::cs_create_file,
        },
        req_frame->id);
    co_return;
}

static auto cs_append_data_handle(std::shared_ptr<connection_t> conn, std::shared_ptr<proto_frame_t> req_frame) -> asio::awaitable<void> {
    auto file_id = conn->get_data<uint64_t>(conn_data::c_file_id);
    if (!file_id) {
        g_log->log_warn(std::format("client {} not create file yield", conn->to_string()));
        co_await conn->send_res_frame(
            proto_frame_t{
                .cmd = (uint8_t)proto_cmd_e::cs_append_data,
                .stat = 1,
            },
            req_frame->id);
        co_return;
    }

    /* append data */
    auto data = std::span<uint8_t>{(uint8_t *)req_frame->data, req_frame->data_len};
    if (!hot_stores->write_file(file_id.value(), data)) {
        g_log->log_error("failed to write file");
        co_await conn->send_res_frame(
            proto_frame_t{
                .cmd = (uint8_t)proto_cmd_e::cs_append_data,
                .stat = 2,
            },
            req_frame->id);
        co_return;
    }

    /* response */
    co_await conn->send_res_frame(
        proto_frame_t{
            .cmd = (uint8_t)proto_cmd_e::cs_append_data,
        },
        req_frame->id);

    co_return;
}

static auto cs_close_file_handle(std::shared_ptr<connection_t> conn, std::shared_ptr<proto_frame_t> req_frame) -> asio::awaitable<void> {
    auto file_id = conn->get_data<uint64_t>(conn_data::c_file_id);
    if (!file_id) {
        g_log->log_warn(std::format("client {} not create file yield", conn->to_string()));
        co_await conn->send_res_frame(
            proto_frame_t{
                .cmd = (uint8_t)proto_cmd_e::cs_append_data,
                .stat = 1,
            },
            req_frame->id);
        co_return;
    }

    /* close file */
    auto file_name = std::string{(char *)req_frame->data, req_frame->data_len};
    auto new_file_path = hot_stores->close_file(file_id.value(), file_name);
    if (!new_file_path) {
        g_log->log_error("failed to close file");
        co_await conn->send_res_frame(
            proto_frame_t{
                .cmd = (uint8_t)proto_cmd_e::cs_close_file,
                .stat = 2,
            },
            req_frame->id);
        co_return;
    }
    new_file_path = std::format("{}/{}", storage_group_id, new_file_path.value());

    /* response */
    auto res_frame = (proto_frame_t *)malloc(sizeof(proto_frame_t) + new_file_path->size());
    *res_frame = {
        .cmd = (uint8_t)proto_cmd_e::cs_close_file,
        .data_len = (uint32_t)new_file_path->size(),
    };
    std::memcpy(res_frame->data, new_file_path->data(), new_file_path->size());
    co_await conn->send_res_frame(std::shared_ptr<proto_frame_t>{res_frame, [](auto p) { free(p); }}, req_frame->id);
    co_return;
}

static auto client_req_handles = std::map<proto_cmd_e, req_handle_t>{
    {(proto_cmd_e)proto_cmd_e::cs_create_file, cs_create_file_handle},
    {(proto_cmd_e)proto_cmd_e::cs_append_data, cs_append_data_handle},
    {(proto_cmd_e)proto_cmd_e::cs_close_file, cs_close_file_handle},
};

static auto request_from_client(std::shared_ptr<connection_t> conn) -> asio::awaitable<void> {
    while (true) {
        auto req_frame = co_await conn->recv_req_frame();
        if (!req_frame) {
            co_return co_await on_client_disconnect(conn);
        }

        /* storage 注册后视为 storage 处理 */
        if (req_frame->cmd == (uint8_t)proto_cmd_e::ss_regist) {
            co_return co_await ss_regist_handle(conn, req_frame);
        }

        auto handle = client_req_handles.find((proto_cmd_e)req_frame->cmd);
        if (handle == client_req_handles.end()) {
            g_log->log_warn(std::format("unhandle cmd {} from client {}", req_frame->cmd, conn->to_string()));
            continue;
        }
        co_await handle->second(conn, req_frame);
    }
}

/* 作为服务器，监听 client 和 storage 的连接 */
static auto work_as_server() -> asio::awaitable<void> {
    /* start acceptor */
    auto acceptor = acceptor_t{co_await asio::this_coro::executor, acceptor_conf_t{
                                                                       .ip = conf.ip,
                                                                       .port = conf.port,
                                                                       .heart_timeout = conf.heart_timeout,
                                                                       .heart_interval = conf.heart_interval,
                                                                       .log = g_log,
                                                                   }};
    g_log->log_info(std::format("storage service listen on {}", acceptor.to_string()));

    /* create executors */
    for (auto i = 0; i < conf.thread_count; ++i) {
        ss_ios.push_back(std::make_shared<asio::io_context>());
        ss_ios_guard.push_back(asio::make_work_guard(*ss_ios.back()));
        std::thread{[i] {
            ss_ios[i]->run();
        }}.detach();
    }

    /* dispatch connection to executor */
    auto idx = 0ull;
    while (true) {
        auto conn = co_await acceptor.accept();
        auto io = ss_ios[idx++ % ss_ios.size()];
        asio::co_spawn(*io, conn->start(), asio::detached);
        asio::co_spawn(*io, request_from_client(conn), asio::detached);
    }
}

/************************************************************************************************************* */
/************************************************************************************************************* */
/* 处理 master 消息 */

/* 注册到 master */
static auto regist_to_master() -> asio::awaitable<bool> {
    /* connect to master */
    for (auto i = 1;; i *= 2) {
        master_conn = co_await connection_t::connect_to(conf.master_ip, conf.master_port, g_log);
        if (!master_conn) {
            g_log->log_error(std::format("failed to connect to master {}:{}", conf.master_ip, conf.master_port));
            co_await co_sleep_for(std::chrono::seconds{i});
            continue;
        }
        break;
    }

    /* regist to master */
    auto req_data = dfs::proto::sm_regist::request_t{};
    req_data.mutable_storage_info()->set_id(conf.id);
    req_data.mutable_storage_info()->set_port(conf.port);
    req_data.mutable_storage_info()->set_magic(conf.storage_magic);
    req_data.mutable_storage_info()->set_ip(conf.ip);
    req_data.set_master_magic(conf.master_magic);

    auto req_frame = (proto_frame_t *)malloc(sizeof(proto_frame_t) + req_data.ByteSizeLong());
    *req_frame = {
        .cmd = (uint8_t)proto_cmd_e::sm_regist,
        .data_len = (uint32_t)req_data.ByteSizeLong(),
    };
    if (!req_data.SerializeToArray(req_frame->data, req_frame->data_len)) {
        g_log->log_error("failed to serialize sm_regist request");
        co_return false;
    }
    auto id = co_await master_conn->send_req_frame(std::shared_ptr<proto_frame_t>{req_frame, [](auto p) { free(p); }});
    if (!id) {
        g_log->log_error(std::format("failed to send regist to master {}", master_conn->to_string()));
        co_return false;
    }

    /* wait regist response */
    auto res_frame = co_await master_conn->recv_res_frame(id.value());
    if (!res_frame || res_frame->cmd != (uint8_t)proto_cmd_e::sm_regist || res_frame->stat != 0) {
        g_log->log_error(std::format("failed to regist to master {} {}", master_conn->to_string(), res_frame->stat));
        co_return false;
    }

    /* parse response */
    auto res_data = dfs::proto::sm_regist::response_t{};
    if (!res_data.ParseFromArray(res_frame->data, res_frame->data_len)) {
        g_log->log_error("failed to parse sm_regist response");
        co_return false;
    }
    storage_group_id = res_data.storage_group();
    g_log->log_info(std::format("regist to master {} suc group {}", master_conn->to_string(), storage_group_id));

    /* regist to other storage */
    for (const auto &info : res_data.storage_info()) {
        /* connect to storage */
        auto conn = co_await connection_t::connect_to(info.ip(), info.port(), g_log);
        if (!conn) {
            g_log->log_error(std::format("failed to connect to storage {}:{}", info.ip(), info.port()));
            continue;
        }
        g_log->log_info(std::format("connect to storage {} {} suc", conn->to_string(), info.magic()));

        /* regist to storage */
        auto req_frame = (proto_frame_t *)malloc(sizeof(proto_frame_t) + sizeof(uint32_t));
        *req_frame = {
            .cmd = (uint8_t)proto_cmd_e::ss_regist,
            .data_len = sizeof(uint32_t),
        };
        *((uint32_t *)req_frame->data) = info.magic();

        auto id = co_await conn->send_req_frame(std::shared_ptr<proto_frame_t>{req_frame, [](auto p) { free(p); }});
        if (!id) {
            g_log->log_error(std::format("failed regist to storage {}", conn->to_string()));
            continue;
        }

        /* wait regist response */
        auto res_frame = co_await conn->recv_res_frame(id.value());
        if (!res_frame || res_frame->cmd != (uint8_t)proto_cmd_e::ss_regist || res_frame->stat != 0) {
            g_log->log_error(std::format("failed regist to storage {} {}", conn->to_string(), res_frame->stat));
            continue;
        }

        /* regist suc */
        storage_conns.insert(conn);
        g_log->log_info(std::format("regist to storage {} suc", conn->to_string()));
        asio::co_spawn(co_await asio::this_coro::executor, request_from_storage(conn), asio::detached);
    }

    co_return true;
}

static auto ms_fs_free_size_handle(std::shared_ptr<connection_t> conn, std::shared_ptr<proto_frame_t> req_frame) -> asio::awaitable<void> {
    auto max = (uint64_t)0;
    for (const auto &path : conf.hot_paths) {
        auto [free, total] = fs_free_size(path);
        max = std::max(max, free);
    }

    auto res_frame = (proto_frame_t *)malloc(sizeof(proto_frame_t) + sizeof(uint64_t));
    *res_frame = {
        .cmd = (uint8_t)proto_cmd_e::ms_fs_free_size,
        .data_len = sizeof(uint64_t),
    };
    *((uint64_t *)res_frame->data) = max;
    co_await conn->send_res_frame(std::shared_ptr<proto_frame_t>{res_frame, [](auto p) { free(p); }},
                                  req_frame->id);
    co_return;
}

static auto master_req_handles = std::map<proto_cmd_e, req_handle_t>{
    {(proto_cmd_e)proto_cmd_e::ms_fs_free_size, ms_fs_free_size_handle},
};

static auto request_from_master() -> asio::awaitable<void> {
    /* regist to master first */
    if (!co_await regist_to_master()) {
        if (master_conn) {
            master_conn->close();
            master_conn = nullptr;
        }
        co_return;
    }

    /* recv request from master */
    while (true) {
        auto req_frame = co_await master_conn->recv_req_frame();
        if (!req_frame) {
            g_log->log_error(std::format("master {} disconnect", master_conn->to_string()));
            master_conn = nullptr;
            break;
        }

        auto handle = master_req_handles.find((proto_cmd_e)req_frame->cmd);
        if (handle == master_req_handles.end()) {
            g_log->log_error(std::format("unhandle cmd {} from master {}", req_frame->cmd, master_conn->to_string()));
            co_await master_conn->send_res_frame(
                proto_frame_t{
                    .cmd = req_frame->cmd,
                    .stat = UINT8_MAX,
                },
                req_frame->id);
            continue;
        }
        co_await handle->second(master_conn, req_frame);
    }
}

auto storage_service() -> asio::awaitable<void> {
    g_log->log_info("storage service start");
    init_conf();
    init_stores();
    asio::co_spawn(co_await asio::this_coro::executor, work_as_server(), asio::detached);
    asio::co_spawn(co_await asio::this_coro::executor, request_from_master(), asio::detached);

    co_return;
}