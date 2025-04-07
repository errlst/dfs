#include <asio.hpp>
#include <common/json.h>
#include <common/log.h>
#include <common/metrics_mem.h>
#include <common/util.h>
#include <fstream>
#include <sstream>

namespace common_detail
{

  using namespace common;

  auto parse_proc_memnfio() -> void
  {
    auto ifs = std::ifstream{"/proc/meminfo"};
    auto line = std::string{};
    while (std::getline(ifs, line))
    {
      auto iss = std::istringstream{line};
      auto name = std::string{};
      iss >> name;
      if (name == "MemTotal:")
      {
        iss >> mem_metrics.total;
        mem_metrics.total *= 1_KB;
      }
      else if (name == "MemAvailable:")
      {
        iss >> mem_metrics.available;
        mem_metrics.available *= 1_KB;
      }
    }
  }

  auto do_mem_metrics() -> asio::awaitable<void>
  {
    auto timer = asio::steady_timer{co_await asio::this_coro::executor};
    while (true)
    {
      timer.expires_after(std::chrono::seconds{1});
      co_await timer.async_wait(asio::use_awaitable);

      parse_proc_memnfio();

      auto lock = std::unique_lock{mem_metrics_lock};
      mem_metrics_bk = mem_metrics;
    }
  }

} // namespace common_detail

namespace common
{

  using namespace common_detail;

  auto start_mem_metrics() -> asio::awaitable<void>
  {
    asio::co_spawn(co_await asio::this_coro::executor, do_mem_metrics(), asio::detached);
  }

  auto get_mem_metrics() -> nlohmann::json
  {
    return {
        {"total", mem_metrics_bk.total},
        {"available", mem_metrics_bk.available},
    };
  }

} // namespace common