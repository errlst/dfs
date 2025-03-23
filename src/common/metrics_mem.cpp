module;

#include "log.h"
#include <asio.hpp>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

module common.metrics.mem;

import common.util;
import common.exception;

namespace common {

  auto parse_proc_memnfio() -> void {
    auto ifs = std::ifstream{"/proc/meminfo"};
    auto line = std::string{};
    while (std::getline(ifs, line)) {
      auto iss = std::istringstream{line};
      auto name = std::string{};
      iss >> name;
      if (name == "MemTotal:") {
        iss >> mem_metrics.total;
        mem_metrics.total *= 1_KB;
      } else if (name == "MemAvailable:") {
        iss >> mem_metrics.available;
        mem_metrics.available *= 1_KB;
      }
    }
  }

  auto do_mem_metrics() -> asio::awaitable<void> {
    auto timer = asio::steady_timer{co_await asio::this_coro::executor};
    while (true) {
      timer.expires_after(std::chrono::seconds{1});
      co_await timer.async_wait(asio::use_awaitable);

      parse_proc_memnfio();

      auto lock = std::unique_lock{mem_metrics_lock};
      mem_metrics_bk = mem_metrics;
    }
  }

  auto start_mem_metrics() -> asio::awaitable<void> {
    LOG_INFO("start mem metrics");
    asio::co_spawn(co_await asio::this_coro::executor, do_mem_metrics(), common::exception_handle);
  }

  auto get_mem_metrics() -> nlohmann::json {
    return {
        {"total", mem_metrics_bk.total},
        {"available", mem_metrics_bk.available},
    };
  }

} // namespace common