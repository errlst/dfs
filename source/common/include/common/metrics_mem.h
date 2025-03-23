#pragma once

#include <asio.hpp>
#include <mutex>
#include <nlohmann/json.hpp>

namespace common_detail {
  
  inline struct mem_metrics_t {
    uint64_t total;
    uint64_t available;
  } mem_metrics, mem_metrics_bk;

  inline auto mem_metrics_lock = std::mutex{};

  /**
   * @brief 解析 /proc/meminfo
   *
   */
  auto parse_proc_memnfio() -> void;

  auto do_mem_metrics() -> asio::awaitable<void>;

} // namespace common_detail

namespace common {

  auto start_mem_metrics() -> asio::awaitable<void>;

  auto get_mem_metrics() -> nlohmann::json;

} // namespace common