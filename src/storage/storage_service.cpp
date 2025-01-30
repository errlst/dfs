#include "./storage_service_global.h"

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
        asio::co_spawn(*io, recv_from_client(conn), asio::detached);
    }
}

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

auto storage_service() -> asio::awaitable<void> {
    g_log->log_info("storage service start");
    init_conf();
    init_stores();
    asio::co_spawn(co_await asio::this_coro::executor, work_as_server(), asio::detached);
    asio::co_spawn(co_await asio::this_coro::executor, recv_from_master(), asio::detached);

    co_return;
}