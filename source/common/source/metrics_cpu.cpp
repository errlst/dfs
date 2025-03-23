#include <common/log.h>
#include <common/metrics_cpu.h>
#include <fstream>

namespace common_detail {

  auto parse_proc_stat() -> void {
    auto ifs = std::ifstream{"/proc/stat"};
    auto line = std::string{};
    auto core_idx = 0;
    while (std::getline(ifs, line)) {
      auto iss = std::istringstream{line};
      auto name = std::string{};
      iss >> name;
      double user, nice, system, idle, iowait, irq, softirq, steal;
      if (name == "cpu") {
        iss >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;
        auto current_total_ticks = user + nice + system + idle + iowait + irq + softirq + steal;
        if (cpu_metrics.all.last_total_ticks != 0) {
          auto delta_total = current_total_ticks - cpu_metrics.all.last_total_ticks;
          auto delta_idle = idle - cpu_metrics.all.last_idle_ticks;
          cpu_metrics.usage_percent = (uint64_t)(10000.0 * (delta_total - delta_idle) / delta_total) / 100.;
        }
        cpu_metrics.all.last_total_ticks = current_total_ticks;
        cpu_metrics.all.last_idle_ticks = idle;
      } else if (name.starts_with("cpu")) {
        if (core_idx >= cpu_metrics.cores.size()) {
          cpu_metrics.cores.emplace_back();
          cpu_metrics.core_usages_percent.emplace_back();
        }
        iss >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;
        auto current_total_ticks = user + nice + system + idle + iowait + irq + softirq + steal;
        if (cpu_metrics.cores[core_idx].last_total_ticks != 0) {
          auto delta_total = current_total_ticks - cpu_metrics.cores[core_idx].last_total_ticks;
          auto delta_idle = idle - cpu_metrics.cores[core_idx].last_idle_ticks;
          cpu_metrics.core_usages_percent[core_idx] = (uint64_t)(10000.0 * (delta_total - delta_idle) / delta_total) / 100.;
        }
        cpu_metrics.cores[core_idx].last_total_ticks = current_total_ticks;
        cpu_metrics.cores[core_idx].last_idle_ticks = idle;
        ++core_idx;
      } else {
        break;
      }
    }
  }

  auto parse_proc_loadavg() -> void {
    auto ifs = std::ifstream{"/proc/loadavg"};
    auto line = std::string{};
    std::getline(ifs, line);
    std::istringstream iss{line};
    iss >> cpu_metrics.load_1 >> cpu_metrics.load_5 >> cpu_metrics.load_15;
  }

  auto do_metrics_cpu() -> asio::awaitable<void> {
    auto timer = asio::steady_timer{co_await asio::this_coro::executor};

    while (true) {
      timer.expires_after(std::chrono::seconds{1});
      co_await timer.async_wait(asio::use_awaitable);

      parse_proc_stat();
      parse_proc_loadavg();

      auto lock = std::unique_lock{cpu_metrics_lock};
      cpu_metrics_bk = cpu_metrics;
    }
  }
} // namespace common_detail
namespace common {

  using namespace common_detail;

  auto start_cpu_metrics() -> asio::awaitable<void> {
    asio::co_spawn(co_await asio::this_coro::executor, do_metrics_cpu(), asio::detached);
  }

  auto get_cpu_metrics() -> nlohmann::json {
    return {
        {"usage_percent", cpu_metrics_bk.usage_percent},
        {"load_1", cpu_metrics_bk.load_1},
        {"load_5", cpu_metrics_bk.load_5},
        {"load_15", cpu_metrics_bk.load_15},
        {"core_usages", cpu_metrics_bk.core_usages_percent},
    };
  }

} // namespace common