#pragma once
#include <asio.hpp>
#include <nlohmann/json.hpp>

namespace metrics {
using metrics_extension = std::function<nlohmann::json()>;

struct metrics_service_config {
  std::string base_path;
  uint32_t interval;
  std::vector<std::pair<std::string, metrics_extension>> extensions;
};

/* 统计请求 */
struct request_end_info {
  bool success;
};
auto push_one_request() -> std::chrono::steady_clock::time_point;
auto pop_one_request(std::chrono::steady_clock::time_point begin_time, request_end_info info) -> void;
auto push_one_connection() -> void;
auto pop_one_connection() -> void;

auto get_metrics_as_string() -> std::string;

auto metrics_service(metrics_service_config conf) -> asio::awaitable<void>;

} // namespace metrics
