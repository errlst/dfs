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
auto push_one_request() -> std::chrono::steady_clock::time_point;
auto pop_one_request(std::chrono::steady_clock::time_point begin_time, request_end_info info) -> void;
auto push_one_connection() -> void;
auto pop_one_connection() -> void;

auto metrics_service(metrics_service_config conf) -> asio::awaitable<void>;

} // namespace metrics
