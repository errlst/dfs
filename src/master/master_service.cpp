#include "./master_service_global.h"

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
        asio::co_spawn(*io, recv_from_client(conn), asio::detached);
    }
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

auto master_service() -> asio::awaitable<void> {
    init_conf();
    asio::co_spawn(co_await asio::this_coro::executor, work_as_master_service(), asio::detached);
    co_return;
}