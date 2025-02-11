#pragma once
#include <asio.hpp>

struct master_service_conf {
  std::string ip;
  uint16_t port;
  uint16_t thread_count;
  uint16_t group_size;
  uint32_t master_magic;
  uint32_t heart_timeout;
  uint32_t heart_interval;
};

auto master_service(master_service_conf config) -> asio::awaitable<void>;
