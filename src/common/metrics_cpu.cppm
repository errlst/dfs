module;

#include <asio.hpp>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <vector>

export module common.metrics.cpu;

namespace common {

  struct cpu_metrics_t {
    double load_1, load_5, load_15;
    double usage_percent;
    std::vector<double> core_usages_percent;

    struct last_info {
      uint64_t last_total_ticks;
      uint64_t last_idle_ticks;
    };
    last_info all;
    std::vector<last_info> cores;
  } cpu_metrics, cpu_metrics_bk;

  auto cpu_metrics_lock = std::mutex{};

  /**
   * @brief 解析 /proc/stat
   *
   */
  auto parse_proc_stat() -> void;

  /**
   * @brief 解析 /proc/loadavg
   *
   */
  auto parse_proc_loadavg() -> void;

} // namespace common

export namespace common {

  auto start_cpu_metrics() -> asio::awaitable<void>;

  auto get_cpu_metrics() -> nlohmann::json;

} // namespace common