#include "./storage_service.h"
#include "../common/listener.h"
#include "./global.h"
#include "./store.h"

struct {
    uint32_t id;
    std::string ip;
    uint16_t port;
    std::string master_ip;
    uint16_t master_port;
    uint16_t thread_count;
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

static auto master_conn = std::shared_ptr<connection_t>{};               // master 服务器连接
static auto storage_conn = std::vector<std::shared_ptr<connection_t>>{}; // 同组 storage 服务器连接

static auto storage_magic = uint32_t{};

static auto init_conf() -> void {
    conf.id = g_conf["storage_service"]["id"].get<uint32_t>();
    conf.ip = g_conf["storage_service"]["ip"].get<std::string>();
    conf.port = g_conf["storage_service"]["port"].get<uint16_t>();
    conf.master_ip = g_conf["storage_service"]["master_ip"].get<std::string>();
    conf.master_port = g_conf["storage_service"]["master_port"].get<uint16_t>();
    conf.thread_count = g_conf["storage_service"]["thread_count"].get<uint16_t>();
    conf.hot_paths = g_conf["storage_service"]["hot_paths"].get<std::vector<std::string>>();
    conf.warm_paths = g_conf["storage_service"]["warm_paths"].get<std::vector<std::string>>();
    conf.cold_paths = g_conf["storage_service"]["cold_paths"].get<std::vector<std::string>>();
    conf.heartbeat_interval = std::chrono::milliseconds{g_conf["network"]["heartbeat_interval"].get<uint32_t>()};
    conf.heartbeat_timeout = std::chrono::milliseconds{g_conf["network"]["heartbeat_timeout"].get<uint32_t>()};
}

static auto init_stores() -> void {
    hot_stores = std::make_shared<store_ctx_group_t>("hot_store_group", conf.hot_paths);
    warm_stores = std::make_shared<store_ctx_group_t>("warm_store_group", conf.warm_paths);
    cold_stores = std::make_shared<store_ctx_group_t>("cold_store_group", conf.cold_paths);
}

/* 作为服务器，处理 client 和 storage 的消息 */
static auto read_as_storage_service(std::shared_ptr<connection_t> conn) -> asio::awaitable<void> {
    while (true) {
        auto frame = co_await conn->recv_req_frame();
        if (frame) {
            switch (frame->cmd) {
            }
        }
    }

    co_return;
}

/* 作为服务器，监听 client 和 storage 的连接 */
static auto work_as_storage_service() -> asio::awaitable<void> {
    auto acceptor = listener_t{co_await asio::this_coro::executor,
                               {listener_conf_t{
                                   .ip = conf.ip,
                                   .port = conf.port,
                                   .log = g_log,
                                   .conn_conf = {
                                       .heartbeat_timeout = conf.heartbeat_timeout,
                                       .heartbeat_interval = conf.heartbeat_interval,
                                       .log = g_log,
                                   }}}};
    g_log->log_info(std::format("storage service listen on {}", acceptor.to_string()));

    for (auto i = 0; i < conf.thread_count; ++i) {
        ss_ios.push_back(std::make_shared<asio::io_context>());
        ss_ios_guard.push_back(asio::make_work_guard(*ss_ios.back()));
        std::thread{[i] {
            ss_ios[i]->run();
        }}.detach();
    }

    auto idx = 0ull;
    while (true) {
        auto conn = co_await acceptor.accept();
        if (conn) {
            auto io = ss_ios[idx++ % ss_ios.size()];
            asio::co_spawn(*io, conn->start(), asio::detached);
            asio::co_spawn(*io, read_as_storage_service(conn), asio::detached);
        }
    }
}

/* 作为客户端，处理 master 消息 */
static auto work_as_master_client() -> asio::awaitable<void> {
    auto timer = asio::steady_timer{co_await asio::this_coro::executor};

    /* connect to master */
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
    asio::co_spawn(co_await asio::this_coro::executor, master_conn->start(), asio::detached);

    /* regist to master */
    auto req_frame = (proto_frame_t *)malloc(sizeof(proto_frame_t) + sizeof(sm_regist_req_t) + conf.ip.size());
    *req_frame = {
        .magic = frame_valid_magic,
        .cmd = (uint8_t)proto_cmd_e::sm_regist,
        .len = sizeof(sm_regist_req_t),
    };
    *(sm_regist_req_t *)req_frame->data = {
        .storage_id = conf.id,
        .storage_magic = storage_magic,
        .master_magic = 0x12345678,
        .port = conf.port,
    };
    memcpy((*(sm_regist_req_t *)req_frame->data).ip, conf.ip.data(), conf.ip.size());
    auto id = co_await master_conn->send_req_frame(std::shared_ptr<proto_frame_t>{req_frame, [](auto p) { free(p); }});
    if (!id) {
        g_log->log_error(std::format("failed to send regist to master {}", master_conn->to_string()));
        co_return;
    }
    g_log->log_debug("send regist to master suc");

    /* for regist response */
    auto res_frame = co_await master_conn->recv_res_frame(id.value());
    if (!res_frame || res_frame->cmd != (uint8_t)proto_cmd_e::sm_regist || res_frame->stat != 0) {
        g_log->log_error(std::format("failed to regist to master {} {}", master_conn->to_string(), res_frame->stat));
        co_return;
    }
    g_log->log_info(std::format("regist to master {} suc", master_conn->to_string()));
}

auto storage_service() -> asio::awaitable<void> {
    g_log->log_info("storage service start");
    storage_magic = std::random_device{}();
    init_conf();
    init_stores();
    asio::co_spawn(co_await asio::this_coro::executor, work_as_storage_service(), asio::detached);
    asio::co_spawn(co_await asio::this_coro::executor, work_as_master_client(), asio::detached);

    co_return;
}