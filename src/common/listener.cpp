#include "listener.h"

listener_t::listener_t(const asio::any_io_executor &io, listener_conf_t conf)
    : m_acceptor{io}, m_conf{conf} {
    auto ep = asio::ip::tcp::endpoint(asio::ip::make_address(conf.ip), conf.port);
    m_acceptor.open(ep.protocol());
    m_acceptor.set_option(asio::socket_base::reuse_address(true));
    m_acceptor.bind(ep);
    m_acceptor.listen();
    m_as_string = std::format("listener {}:{}", conf.ip, conf.port);
}

auto listener_t::accept() -> asio::awaitable<std::shared_ptr<connection_t>> {
    auto [ec, sock] = co_await m_acceptor.async_accept(asio::as_tuple(asio::use_awaitable));
    if (ec) {
        m_conf.log->log_error(std::format("{} accept error {}", to_string(), ec.message()));
        co_return nullptr;
    }
    auto conn = std::make_shared<connection_t>(std::move(sock), m_conf.conn_conf);
    m_conf.log->log_info(std::format("{} accept connection {} ", to_string(), conn->to_string()));
    co_return conn;
}
