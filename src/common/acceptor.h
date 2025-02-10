#pragma once
#include "connection.h"

namespace common {
struct acceptor_conf {
  std::string ip;
  uint16_t port;
  uint32_t h_timeout;
  uint32_t h_interval;
};

class acceptor {
public:
  acceptor(asio::any_io_executor, acceptor_conf conf);

  /* 返回建立心跳的 connection */
  auto accept() -> asio::awaitable<std::shared_ptr<connection>>;

private:
  asio::ip::tcp::acceptor acceptor_;
  acceptor_conf conf_;
};
} // namespace common
