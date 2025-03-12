#include "./metrics_service.h"
#include "../common/log.h"
#include "../common/util.h"
#include <fstream>
#include <nlohmann/json.hpp>
#include <sys/sysinfo.h>

namespace metrics {

static struct {
  std::atomic_uint64_t connection_count;

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
    std::atomic_uint64_t peak; // 同时峰值请求
    uint64_t peak_bk;
  } count_last_second, count_last_minute, count_last_hour, count_last_day, count_since_start, count_sentinel;
} request_metrics;

auto push_one_request() -> std::chrono::steady_clock::time_point {
  auto bt = std::chrono::steady_clock::now();
  ++request_metrics.count_concurrent;
  return bt;
}

auto pop_one_request(std::chrono::steady_clock::time_point begin_time, request_end_info info) -> void {
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
static auto flush_request() -> asio::awaitable<void> {
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

auto push_one_connection() -> void {
  ++request_metrics.connection_count;
}
auto pop_one_connection() -> void {
  --request_metrics.connection_count;
}

/* request 信息到 json */
static auto request_metrics_to_json() -> nlohmann::json {
  static auto convert_atomic_array = []<size_t N>(const std::array<std::atomic_uint64_t, N> &arr) {
    auto ret = std::vector<uint64_t>{};
    for (auto i = 0uz; i < N; ++i) {
      ret.push_back(arr[i].load());
    }
    return ret;
  };
  static auto convert_time_window = [](const auto &time_window) {
    return nlohmann::json{
        {"total", std::max(time_window.total.load(), time_window.total_bk)},
        {"success", std::max(time_window.success.load(), time_window.success_bk)},
        {"peak", std::max(time_window.peak.load(), time_window.peak_bk)}};
  };
  return {
      {"connection_count", request_metrics.connection_count.load()},
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

static struct {
  struct {
    double load_1, load_5, load_15;
    double usage_percent;
    std::vector<double> core_usages_percent;

    struct last_info {
      uint64_t last_total_ticks;
      uint64_t last_idle_ticks;
    };
    last_info all;
    std::vector<last_info> cores;
  } cpu;

  struct {
    uint64_t total, available;
  } mem;

  struct {
    uint64_t total_send, total_recv;
    uint64_t packets_send, packets_recv;
    uint64_t errin, errout;
    uint64_t dropin, dropout;
  } net;

} system_metrics;

static auto read_proc_stat() -> void {
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
      if (system_metrics.cpu.all.last_total_ticks != 0) {
        auto delta_total = current_total_ticks - system_metrics.cpu.all.last_total_ticks;
        auto delta_idle = idle - system_metrics.cpu.all.last_idle_ticks;
        system_metrics.cpu.usage_percent = (uint64_t)(10000.0 * (delta_total - delta_idle) / delta_total) / 100.;
      }
      system_metrics.cpu.all.last_total_ticks = current_total_ticks;
      system_metrics.cpu.all.last_idle_ticks = idle;
    } else if (name.starts_with("cpu")) {
      if (core_idx >= system_metrics.cpu.cores.size()) {
        system_metrics.cpu.cores.emplace_back();
        system_metrics.cpu.core_usages_percent.emplace_back();
      }
      iss >> user >> nice >> system >> idle >> iowait >> irq >> softirq >> steal;
      auto current_total_ticks = user + nice + system + idle + iowait + irq + softirq + steal;
      if (system_metrics.cpu.cores[core_idx].last_total_ticks != 0) {
        auto delta_total = current_total_ticks - system_metrics.cpu.cores[core_idx].last_total_ticks;
        auto delta_idle = idle - system_metrics.cpu.cores[core_idx].last_idle_ticks;
        system_metrics.cpu.core_usages_percent[core_idx] = (uint64_t)(10000.0 * (delta_total - delta_idle) / delta_total) / 100.;
      }
      system_metrics.cpu.cores[core_idx].last_total_ticks = current_total_ticks;
      system_metrics.cpu.cores[core_idx].last_idle_ticks = idle;
      ++core_idx;
    } else {
      break;
    }
  }
}

static auto read_proc_loadavg() -> void {
  auto ifs = std::ifstream{"/proc/loadavg"};
  auto line = std::string{};
  std::getline(ifs, line);
  std::istringstream iss{line};
  iss >> system_metrics.cpu.load_1 >> system_metrics.cpu.load_5 >> system_metrics.cpu.load_15;
}

static auto read_proc_meminfo() -> void {
  auto ifs = std::ifstream{"/proc/meminfo"};
  auto line = std::string{};
  while (std::getline(ifs, line)) {
    auto iss = std::istringstream{line};
    auto name = std::string{};
    iss >> name;
    if (name == "MemTotal:") {
      iss >> system_metrics.mem.total;
      system_metrics.mem.total *= 1_KB;
    } else if (name == "MemAvailable:") {
      iss >> system_metrics.mem.available;
      system_metrics.mem.available *= 1_KB;
    }
  }
}

static auto read_proc_net_dev() -> void {
  auto ifs = std::ifstream{"/proc/net/dev"};
  auto line = std::string{};
  std::getline(ifs, line);
  std::getline(ifs, line);
  system_metrics.net = {};
  while (std::getline(ifs, line)) {
    auto iss = std::istringstream{line};
    auto name = std::string{};
    iss >> name;
    if (name == "lo") {
      continue;
    }

    uint64_t bytes, packets, err, drop, ignore;
    iss >> bytes >> packets >> err >> drop >> ignore >> ignore >> ignore >> ignore;
    system_metrics.net.total_recv += bytes;
    system_metrics.net.packets_recv += packets;
    system_metrics.net.errin += err;
    system_metrics.net.dropin += drop;
    iss >> bytes >> packets >> err >> drop >> ignore >> ignore >> ignore >> ignore;
    system_metrics.net.total_send += bytes;
    system_metrics.net.packets_send += packets;
    system_metrics.net.errin += err;
    system_metrics.net.dropin += drop;
    break;
  }
}

static auto system_metrics_to_json() -> nlohmann::json {
  read_proc_stat();
  read_proc_loadavg();
  read_proc_meminfo();
  read_proc_net_dev();

  return {
      {"cpu", {
                  {"usage_percent", system_metrics.cpu.usage_percent},
                  {"load_1", system_metrics.cpu.load_1},
                  {"load_5", system_metrics.cpu.load_5},
                  {"load_15", system_metrics.cpu.load_15},
                  {"core_usages", system_metrics.cpu.core_usages_percent},
              }},
      {"mem", {
                  {"total", system_metrics.mem.total},
                  {"available", system_metrics.mem.available},
              }},
      {"net", {
                  {"total_send", system_metrics.net.total_send},
                  {"total_recv", system_metrics.net.total_recv},
                  {"packets_send", system_metrics.net.packets_send},
                  {"packets_recv", system_metrics.net.packets_recv},
                  {"errin", system_metrics.net.errin},
                  {"errout", system_metrics.net.errout},
                  {"dropin", system_metrics.net.dropin},
                  {"dropout", system_metrics.net.dropout},
              }},
  };
}

static auto metrics_string = std::string{};

static auto metrics_string_mut = std::mutex{};

auto get_metrics_as_string() -> std::string {
  auto lock = std::unique_lock{metrics_string_mut};
  return metrics_string;
}

static auto exts_mut = std::mutex{};

static auto exts = std::map<std::string, std::function<nlohmann::json()>>{};

auto add_metrics_extension(std::string name, std::function<nlohmann::json()> ext) -> asio::awaitable<void> {
  auto lock = std::unique_lock{exts_mut};
  exts[name] = ext;
  co_return;
}

auto metrics_service(const std::string &base_path) -> asio::awaitable<void> {
  asio::co_spawn(co_await asio::this_coro::executor, flush_request(), asio::detached);

  auto timer = asio::steady_timer{co_await asio::this_coro::executor};
  while (true) {
    timer.expires_after(std::chrono::seconds{1});
    co_await timer.async_wait(asio::as_tuple(asio::use_awaitable));

    auto ofs = std::ofstream{base_path + "/data/metrics.json", std::ios::trunc};
    auto json = nlohmann::json{
        {"system_metrics", system_metrics_to_json()},
        {"request_metrics", request_metrics_to_json()},
    };

    auto lock = std::unique_lock{exts_mut};
    for (const auto &[name, ext] : exts) {
      json[name] = ext();
    }

    lock = std::unique_lock{metrics_string_mut};
    metrics_string = json.dump(2);
    ofs.write(metrics_string.data(), metrics_string.size());
  }
}
} // namespace metrics
