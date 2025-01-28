#include "./util.h"
#include "../common/util.h"

auto connect_to_master(std::string_view ip, uint16_t port, std::shared_ptr<log_t> log) -> asio::awaitable<std::shared_ptr<connection_t>> {
    while (true) {
        auto master_conn = co_await connection_t::connect_to("127.0.0.1", 8888, log);
        if (!master_conn) {
            log->log_error("failed to connect to master");
            co_await co_sleep_for(std::chrono::seconds{1});
            continue;
        }
        log->log_info(std::format("connect to master {} suc", master_conn->to_string()));
        co_return master_conn;
    }
    co_return nullptr;
}