#pragma once
#include "connection.h"

namespace common {

class acceptor {
public:
  acceptor(asio::any_io_executor io, const std::string &ip, uint16_t port,
           uint32_t heart_timeout, uint32_t heart_interval);

  /**
   * @brief 接受一个新连接，且建立心跳
   *
   */
  auto accept() -> asio::awaitable<std::shared_ptr<connection>>;

private:
  asio::ip::tcp::acceptor m_acceptor;
  uint32_t m_heart_timeout;
  uint32_t m_heart_interval;
};

} // namespace common
