#include "./storage_service.h"
#include "../common/listener.h"
#include "../proto/proto.h"
#include "./global.h"
#include "./store.h"
#include <set>

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
    std::chrono::milliseconds heartbeat_interval;
    std::chrono::milliseconds heartbeat_timeout;
} static conf;

static std::shared_ptr<store_ctx_group_t> hot_stores;
static std::shared_ptr<store_ctx_group_t> warm_stores;
static std::shared_ptr<store_ctx_group_t> cold_stores;

static auto ss_ios = std::vector<std::shared_ptr<asio::io_context>>{};
static auto ss_ios_guard = std::vector<asio::executor_work_guard<asio::io_context::executor_type>>{};

static auto master_conn = std::shared_ptr<connection_t>{};             // master 服务器连接
static auto storage_conns = std::set<std::shared_ptr<connection_t>>{}; // 同组 storage 服务器连接

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
        .heartbeat_interval = std::chrono::milliseconds{g_conf["network"]["heartbeat_interval"].get<uint32_t>()},
        .heartbeat_timeout = std::chrono::milliseconds{g_conf["network"]["heartbeat_timeout"].get<uint32_t>()},
    };
}

static auto init_stores() -> void {
    hot_stores = std::make_shared<store_ctx_group_t>("hot_store_group", conf.hot_paths);
    warm_stores = std::make_shared<store_ctx_group_t>("warm_store_group", conf.warm_paths);
    cold_stores = std::make_shared<store_ctx_group_t>("cold_store_group", conf.cold_paths);
}

/* 处理注册成功的 storage 消息 */
static auto read_from_storage(std::shared_ptr<connection_t> conn) -> asio::awaitable<void> {
    while (true) {
        if (auto req_frame = co_await conn->recv_req_frame(); req_frame) {
            g_log->log_warn(std::format("unhandle cmd {} from storage {}", req_frame->cmd, conn->to_string()));
        }
    }

    co_return;
}

using req_handle_t = std::function<asio::awaitable<void>(std::shared_ptr<connection_t>, std::shared_ptr<proto_frame_t>)>;

/* 处理 client 消息（storage regist 之前也视为 client 处理 */
static auto read_from_client(std::shared_ptr<connection_t> conn) -> asio::awaitable<void> {
    while (true) {
        if (auto req_frame = co_await conn->recv_req_frame(); req_frame) {
            /* special for storage regist */
            if (req_frame->cmd == (uint8_t)proto_cmd_e::ss_regist) {
                if (req_frame->data_len != sizeof(uint32_t)) {
                    g_log->log_error(std::format("invalid ss_regist data_len {}", (uint32_t)req_frame->data_len));
                    break;
                }
                if (*(uint32_t *)req_frame->data != conf.storage_magic) {
                    g_log->log_error(std::format("invalid ss_regist magic {} {}", conf.storage_magic, *(uint32_t *)req_frame->data));
                    break;
                }

                /* regist suc */
                co_await conn->send_res_frame(proto_frame_t{
                    .cmd = (uint8_t)proto_cmd_e::ss_regist,
                });
                storage_conns.insert(conn);
                g_log->log_info(std::format("storage {} regist suec", conn->to_string()));
                asio::co_spawn(co_await asio::this_coro::executor, read_from_storage(conn), asio::detached);
                co_return;
            }

            /* slove as normal client */
            g_log->log_warn(std::format("unhandle cmd {} from client {}", req_frame->cmd, conn->to_string()));
        }
    }
}

/* 作为服务器，监听 client 和 storage 的连接 */
static auto work_as_storage_service() -> asio::awaitable<void> {
    /* start acceptor */
    auto acceptor = listener_t{co_await asio::this_coro::executor,
                               listener_conf_t{
                                   .ip = conf.ip,
                                   .port = conf.port,
                                   .log = g_log,
                                   .conn_conf = {
                                       .heartbeat_timeout = conf.heartbeat_timeout,
                                       .heartbeat_interval = conf.heartbeat_interval,
                                       .log = g_log,
                                   }}};
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
        if (conn) {
            auto io = ss_ios[idx++ % ss_ios.size()];
            asio::co_spawn(*io, conn->start(), asio::detached);
            asio::co_spawn(*io, read_from_client(conn), asio::detached);
        }
    }
}

/* 注册到 master */
static auto regist_to_master() -> asio::awaitable<bool> {
    /* connect to master */
    auto timer = asio::steady_timer{co_await asio::this_coro::executor};
    for (auto i = 1;; i *= 2) {
        auto [conn, ec] = co_await connection_t::connect_to(co_await asio::this_coro::executor, conf.master_ip, conf.master_port,
                                                            connection_conf_t{
                                                                .heartbeat_timeout = conf.heartbeat_timeout,
                                                                .heartbeat_interval = conf.heartbeat_interval,
                                                                .log = g_log,
                                                            });
        if (ec) {
            g_log->log_error(std::format("failed to connect to master {}:{} {}", conf.ip, conf.port, ec.message()));
        } else {
            master_conn = conn;
            break;
        }
        timer.expires_after(std::chrono::seconds{i});
        co_await timer.async_wait(asio::use_awaitable);
    }
    g_log->log_info(std::format("connect to master {} suc", master_conn->to_string()));

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
    g_log->log_info(std::format("regist to master {} suc", master_conn->to_string()));

    /* parse response */
    auto res_data = dfs::proto::sm_regist::response_t{};
    if (!res_data.ParseFromArray(res_frame->data, res_frame->data_len)) {
        g_log->log_error("failed to parse sm_regist response");
        co_return false;
    }

    /* regist to other storage */
    for (const auto &info : res_data.storage_info()) {
        /* connect to storage */
        auto [conn, ec] = co_await connection_t::connect_to(co_await asio::this_coro::executor, info.ip(), info.port(),
                                                            connection_conf_t{
                                                                .heartbeat_timeout = conf.heartbeat_timeout,
                                                                .heartbeat_interval = conf.heartbeat_interval,
                                                                .log = g_log,
                                                            });
        if (ec) {
            g_log->log_error(std::format("failed to connect to storage {}:{} {}", info.ip(), info.port(), ec.message()));
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
            g_log->log_error(std::format("failed to send im storage to {}", conn->to_string()));
            continue;
        }
        auto res_frame = co_await conn->recv_res_frame(id.value());
        if (!res_frame || res_frame->cmd != (uint8_t)proto_cmd_e::ss_regist || res_frame->stat != 0) {
            g_log->log_error(std::format("failed to im storage to {} {}", conn->to_string(), res_frame->stat));
            continue;
        }
        storage_conns.insert(conn);
        g_log->log_info(std::format("regist to storage {} suc", conn->to_string()));
    }

    co_return true;
}

/* 作为客户端，处理 master 消息 */
static auto work_as_master_client() -> asio::awaitable<void> {
    if (!co_await regist_to_master()) {
        co_return;
    }
}

auto storage_service() -> asio::awaitable<void> {
    g_log->log_info("storage service start");
    init_conf();
    init_stores();
    asio::co_spawn(co_await asio::this_coro::executor, work_as_storage_service(), asio::detached);
    asio::co_spawn(co_await asio::this_coro::executor, work_as_master_client(), asio::detached);

    co_return;
}