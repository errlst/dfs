#pragma once

#include <asio.hpp>
#include <nlohmann/json.hpp>

namespace common_detail {

  inline auto metrics = nlohmann::json{};

  inline auto metrics_lock = std::mutex{};

  auto do_metrics(std::string out_file) -> asio::awaitable<void>;

} // namespace common_detail

namespace common {

  auto start_metrics(const std::string &out_file) -> asio::awaitable<void>;

  auto get_metrics() -> nlohmann::json;

} // namespace common
