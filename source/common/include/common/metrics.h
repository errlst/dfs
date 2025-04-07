#pragma once

#include <asio.hpp>
#include "json.h"

namespace common_detail
{

  inline auto metrics = nlohmann::json{};

  inline auto metrics_lock = std::mutex{};

  inline auto metrics_extensions = std::vector<std::tuple<std::string, std::function<nlohmann::json()>>>{};

  auto do_metrics(std::string out_file) -> asio::awaitable<void>;

} // namespace common_detail

namespace common
{

  auto start_metrics(const std::string &out_file) -> asio::awaitable<void>;

  /**
   * @brief 添加额外的监控任务
   *
   * @param extension <name, work_function>
   */
  auto add_metrics_extension(std::tuple<std::string, std::function<nlohmann::json()>> extension) -> void;

  auto get_metrics() -> nlohmann::json;

} // namespace common
