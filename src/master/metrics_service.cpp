#include "./metrics_service.h"
#include "../common/log.h"
#include "../common/util.h"
#include <fstream>
#include <nlohmann/json.hpp>

namespace metrics {
static metrics_service_config ms_config;

static struct {
  /* 短请求，每个桶表示 10ms，整个范围为 0~100ms */
  std::array<std::atomic_uint64_t, 10> count_in_short_consumption{0};
  /* 中请求，每个桶表示 100ms，整个范围为 100ms~1s */
  std::array<std::atomic_uint64_t, 10> count_in_mid_consumption{0};
  /* 长请求，每个桶表示 1s，整个范围为 1~10s */
  std::array<std::atomic_uint64_t, 10> count_in_long_consumption{0};

  /* 当前正在处理的请求数量 */
  std::atomic_uint64_t count_concurrent;

  /* 一段时间内的请求 */
  struct time_window {
    std::atomic_uint64_t total; // 总请求数量
    uint64_t total_bk;
    std::atomic_uint64_t success; // 成功请求数量
    uint64_t success_bk;
    std::atomic_uint64_t peak; // 峰值请求
    uint64_t peak_bk;
  } count_last_second, count_last_minute, count_last_hour, count_last_day, count_since_start, count_sentinel;
} request_metrics;

auto request_begin() -> std::chrono::steady_clock::time_point {
  auto bt = std::chrono::steady_clock::now();
  ++request_metrics.count_concurrent;
  // LOG_INFO(std::format("{}", request_metrics.count_concurrent.load()));
  return bt;
}

auto request_end(std::chrono::steady_clock::time_point begin_time, request_end_info info) -> void {
  auto consumption = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - begin_time).count();
  if (consumption < 100) {
    ++request_metrics.count_in_short_consumption[consumption / 10];
  } else if (consumption < 1000) {
    ++request_metrics.count_in_mid_consumption[consumption / 100];
  } else {
    ++request_metrics.count_in_long_consumption[std::min(consumption / 1000, 9l)];
  }

  for (auto ptr = &request_metrics.count_last_second; ptr != &request_metrics.count_sentinel; ++ptr) {
    ++ptr->total;
    ptr->success += info.success;
    ptr->peak = std::max(ptr->peak.load(), request_metrics.count_concurrent.load());
  }

  --request_metrics.count_concurrent;
}

/* 定期重置 request 信息 */
auto flush_request() -> asio::awaitable<void> {
  auto timer = asio::steady_timer{co_await asio::this_coro::executor};
  auto times = 0;
  while (true) {
    timer.expires_after(std::chrono::seconds{1});
    co_await timer.async_wait(asio::as_tuple(asio::use_awaitable));
    ++times;

    request_metrics.count_last_second.total_bk = request_metrics.count_last_second.total.load();
    request_metrics.count_last_second.success_bk = request_metrics.count_last_second.success.load();
    request_metrics.count_last_second.peak_bk = request_metrics.count_last_second.peak.load();
    request_metrics.count_last_second.total = request_metrics.count_last_second.success = 0;
    request_metrics.count_last_second.peak = request_metrics.count_concurrent.load();

    if (times % 1_minute == 0) {
      request_metrics.count_last_minute.total_bk = request_metrics.count_last_minute.total.load();
      request_metrics.count_last_minute.success_bk = request_metrics.count_last_minute.success.load();
      request_metrics.count_last_minute.peak_bk = request_metrics.count_last_minute.peak.load();
      request_metrics.count_last_minute.total = request_metrics.count_last_minute.success = 0;
      request_metrics.count_last_minute.peak = request_metrics.count_concurrent.load();
    }

    if (times % 1_hour == 0) {
      request_metrics.count_last_hour.total_bk = request_metrics.count_last_hour.total.load();
      request_metrics.count_last_hour.success_bk = request_metrics.count_last_hour.success.load();
      request_metrics.count_last_hour.peak_bk = request_metrics.count_last_hour.peak.load();
      request_metrics.count_last_hour.total = request_metrics.count_last_hour.success = 0;
      request_metrics.count_last_hour.peak = request_metrics.count_concurrent.load();
    }

    if (times % 1_day == 0) {
      request_metrics.count_last_day.total_bk = request_metrics.count_last_day.total.load();
      request_metrics.count_last_day.success_bk = request_metrics.count_last_day.success.load();
      request_metrics.count_last_day.peak_bk = request_metrics.count_last_day.peak.load();
      request_metrics.count_last_day.total = request_metrics.count_last_day.success = 0;
      request_metrics.count_last_day.peak = request_metrics.count_concurrent.load();
    }
  }
}

/* request 信息到 json */
auto request_metrics_to_json() -> nlohmann::json {
  static auto convert_atomic_array = []<size_t N>(const std::array<std::atomic_uint64_t, N> &arr) {
    auto ret = std::vector<uint64_t>{};
    for (auto i = 0; i < N; ++i) {
      ret.push_back(arr[i].load());
    }
    return ret;
  };
  static auto convert_time_window = [](const auto &time_window) {
    // return nlohmann::json{
    //     {"total", time_window.total_bk},
    //     {"success", time_window.success_bk},
    //     {"peak", time_window.peak_bk}};
    return nlohmann::json{
        {"total", std::max(time_window.total.load(), time_window.total_bk)},
        {"success", std::max(time_window.success.load(), time_window.success_bk)},
        {"peak", std::max(time_window.peak.load(), time_window.peak_bk)}};
  };
  return {
      {"count_in_short_consumption", convert_atomic_array(request_metrics.count_in_short_consumption)},
      {"count_in_mid_consumption", convert_atomic_array(request_metrics.count_in_mid_consumption)},
      {"count_in_long_consumption", convert_atomic_array(request_metrics.count_in_long_consumption)},
      {"count_concurrent", request_metrics.count_concurrent.load()},
      {"count_last_second", convert_time_window(request_metrics.count_last_second)},
      {"count_last_minute", convert_time_window(request_metrics.count_last_minute)},
      {"count_last_hour", convert_time_window(request_metrics.count_last_hour)},
      {"count_last_day", convert_time_window(request_metrics.count_last_day)},
      {"count_since_start", convert_time_window(request_metrics.count_since_start)},
  };
}

auto metrics() -> asio::awaitable<void> {
  asio::co_spawn(co_await asio::this_coro::executor, flush_request(), asio::detached);
  auto timer = asio::steady_timer{co_await asio::this_coro::executor};
  while (true) {
    timer.expires_after(std::chrono::milliseconds{ms_config.interval});
    co_await timer.async_wait(asio::as_tuple(asio::use_awaitable));

    auto ofs = std::ofstream{ms_config.base_path + "/data/metrics.json"};
    auto str = nlohmann::json{{"request_metrics", request_metrics_to_json()}}.dump(2);
    ofs.write(str.data(), str.size());
  }
}

auto metrics_service(metrics_service_config conf) -> asio::awaitable<void> {
  ms_config = conf;
  asio::co_spawn(co_await asio::this_coro::executor, metrics(), asio::detached);
  co_return;
}
} // namespace metrics
