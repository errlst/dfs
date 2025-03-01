#pragma once
#include "connection.h"

namespace common {

struct acceptor_config {
  std::string ip;
  uint16_t port;
  uint32_t h_timeout;
  uint32_t h_interval;
};

class acceptor {
public:
  acceptor(asio::any_io_executor, acceptor_config conf);

  /**
   * @brief 接受一个新连接，且建立心跳
   *
   */
  auto accept() -> asio::awaitable<std::shared_ptr<connection>>;

private:
  asio::ip::tcp::acceptor m_acceptor;
  acceptor_config m_config;
};

} // namespace common
