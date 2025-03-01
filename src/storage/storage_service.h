#pragma once
#include "../common/metrics_service.h"
#include <asio.hpp>

struct storage_service_config {
  uint32_t id;
  uint32_t group_id;
  std::string ip;
  uint16_t port;
  std::string master_ip;
  uint16_t master_port;
  uint16_t thread_count;
  uint16_t storage_magic;
  uint32_t master_magic;
  uint32_t sync_interval;
  std::vector<std::string> hot_paths;
  std::vector<std::string> cold_paths;
  uint32_t heart_timeout;
  uint32_t heart_interval;
};

auto storage_metrics() -> nlohmann::json;

auto storage_service(const nlohmann::json &json) -> asio::awaitable<void>;