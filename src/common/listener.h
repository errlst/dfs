#pragma once
#include "connection.h"

struct listener_conf_t {
    std::string ip;
    uint16_t port;
    std::shared_ptr<log_t> log;
    connection_conf_t conn_conf;
};

class listener_t {
  public:
    listener_t(const asio::any_io_executor& io, listener_conf_t conf);

    auto accept() -> asio::awaitable<std::shared_ptr<connection_t>>;

    auto to_string() -> std::string { return m_as_string; }

  private:
    asio::ip::tcp::acceptor m_acceptor;
    listener_conf_t m_conf;
    std::string m_as_string;
};