#pragma once

#include "json.h"
#include <asio.hpp>
#include <mutex>

namespace common_detail
{

  inline struct net_metrics_t
  {
    uint64_t total_send, total_recv;
    uint64_t packets_send, packets_recv;
    uint64_t errin, errout;
    uint64_t dropin, dropout;
  } net_metrics, met_metrics_bk;

  inline auto net_metrics_lock = std::mutex{};

  /**
   * @brief 解析 /proc/net/dev
   *
   */
  auto parse_proc_net_dev() -> void;

  auto do_net_metrics() -> asio::awaitable<void>;

} // namespace common_detail

namespace common
{

  auto start_net_metrics() -> asio::awaitable<void>;

  auto get_net_metrics() -> nlohmann::json;

} // namespace common