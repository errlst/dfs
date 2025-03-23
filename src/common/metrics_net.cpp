module;

#include "log.h"
#include <asio.hpp>
#include <fstream>
#include <nlohmann/json.hpp>
#include <sstream>

module common.metrics.net;

import common.exception;

namespace common {
  auto parse_proc_net_dev() -> void {
    auto ifs = std::ifstream{"/proc/net/dev"};
    auto line = std::string{};
    std::getline(ifs, line);
    std::getline(ifs, line);

    while (std::getline(ifs, line)) {
      auto iss = std::istringstream{line};
      auto name = std::string{};
      iss >> name;
      if (name == "lo") {
        continue;
      }

      uint64_t bytes, packets, err, drop, ignore;
      iss >> bytes >> packets >> err >> drop >> ignore >> ignore >> ignore >> ignore;
      net_metrics.total_recv += bytes;
      net_metrics.packets_recv += packets;
      net_metrics.errin += err;
      net_metrics.dropin += drop;
      iss >> bytes >> packets >> err >> drop >> ignore >> ignore >> ignore >> ignore;
      net_metrics.total_send += bytes;
      net_metrics.packets_send += packets;
      net_metrics.errin += err;
      net_metrics.dropin += drop;
      break;
    }
  }

  auto do_net_metrics() -> asio::awaitable<void> {
    auto timer = asio::steady_timer{co_await asio::this_coro::executor};
    while (true) {
      timer.expires_after(std::chrono::seconds{1});
      co_await timer.async_wait(asio::use_awaitable);

      parse_proc_net_dev();

      auto lock = std::unique_lock{net_metrics_lock};
      met_metrics_bk = net_metrics;
    }
  }

  auto start_net_metrics() -> asio::awaitable<void> {
    LOG_INFO("start net metrics");
    asio::co_spawn(co_await asio::this_coro::executor, do_net_metrics(), common::exception_handle);
  }

  auto get_net_metrics() -> nlohmann::json {
    return {
        {"total_send", met_metrics_bk.total_send},
        {"total_recv", met_metrics_bk.total_recv},
        {"packets_send", met_metrics_bk.packets_send},
        {"packets_recv", met_metrics_bk.packets_recv},
        {"errin", met_metrics_bk.errin},
        {"errout", met_metrics_bk.errout},
        {"dropin", met_metrics_bk.dropin},
        {"dropout", met_metrics_bk.dropout},
    };
  }

} // namespace common