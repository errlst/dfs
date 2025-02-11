#pragma once
#include <asio.hpp>

namespace metrics {
struct metrics_service_config {
  std::string base_path;
  uint32_t interval;
};

/* 统计请求 */
struct request_end_info {
  bool success;
};
auto request_begin() -> std::chrono::steady_clock::time_point;
auto request_end(std::chrono::steady_clock::time_point begin_time, request_end_info info) -> void;

auto metrics_service(metrics_service_config conf) -> asio::awaitable<void>;

} // namespace metrics
