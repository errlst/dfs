module;

#include "log.h"
#include <asio.hpp>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>

module common.metrics;

import common.exception;

namespace common {

  auto do_metrics(std::string out_file) -> asio::awaitable<void> {
    auto timer = asio::steady_timer{co_await asio::this_coro::executor};

    while (true) {
      timer.expires_after(std::chrono::seconds{1});
      co_await timer.async_wait(asio::use_awaitable);

      auto req_ = get_request_metrics();
      auto cpu_ = get_cpu_metrics();
      auto mem_ = get_mem_metrics();
      auto net_ = get_net_metrics();

      auto lock = std::unique_lock{metrics_lock};
      metrics = {
          {"request", req_},
          {"cpu", cpu_},
          {"mem", mem_},
          {"net", net_},
      };

      auto ofs = std::ofstream{out_file, std::ios::trunc};
      if (!ofs.is_open()) {
        LOG_CRITICAL("metrics open file {} failed", out_file);
      }
      ofs << metrics.dump(2);
    }
  }

  auto start_metrics(const std::string &out_file) -> asio::awaitable<void> {
    LOG_INFO("start metrics output {}", out_file);
    co_await start_request_metrics();
    co_await start_cpu_metrics();
    co_await start_mem_metrics();
    co_await start_net_metrics();

    asio::co_spawn(co_await asio::this_coro::executor, do_metrics(out_file), asio::detached);
  }

  auto get_metrics() -> nlohmann::json {
    auto lock = std::unique_lock{metrics_lock};
    return metrics;
  }

} // namespace common