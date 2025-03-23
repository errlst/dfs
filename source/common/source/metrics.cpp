#include <asio.hpp>
#include <common/log.h>
#include <common/metrics.h>
#include <common/metrics_cpu.h>
#include <common/metrics_mem.h>
#include <common/metrics_net.h>
#include <common/metrics_request.h>
#include <fstream>
#include <nlohmann/json.hpp>
#include <string>

namespace common_detail {

  using namespace common;

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

      auto ofs = std::ofstream{out_file};
      if (!ofs.is_open()) {
        LOG_CRITICAL("metrics open file {} failed", out_file);
      }

      ofs << metrics.dump(2);
      ofs.close();
    }
  }

} // namespace common_detail

namespace common {

  using namespace common_detail;

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