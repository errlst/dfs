#pragma once
#include "connection.h"

struct acceptor_conf_t {
    std::string ip;
    uint16_t port;
    uint32_t heart_timeout;
    uint32_t heart_interval;
    std::shared_ptr<log_t> log;
};

class acceptor_t {
  public:
    acceptor_t(const asio::any_io_executor &io, acceptor_conf_t conf);

    /* 返回建立心跳的 connection */
    auto accept() -> asio::awaitable<std::shared_ptr<connection_t>>;

    auto to_string() -> std::string { return m_as_string; }

  private:
    asio::ip::tcp::acceptor m_acceptor;
    acceptor_conf_t m_conf;
    std::string m_as_string;
};