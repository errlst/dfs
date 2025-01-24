#include "loop.h"

auto loop_t::regist_service(service_t service) -> void {
    m_services.emplace_back(service);
}
auto loop_t::run() -> void {
    for (auto &service : m_services) {
        asio::co_spawn(m_io, service(), asio::detached);
    }
    m_io.run();
}
