#pragma once
#include <asio.hpp>

using service_t = std::function<asio::awaitable<void>()>;

class loop_t {
  public:
    auto regist_service(service_t service) -> void;

    auto run() -> void;

  private:
    asio::io_context m_io;
    std::vector<service_t> m_services;
};