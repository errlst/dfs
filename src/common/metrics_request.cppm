module;

#include <array>
#include <asio.hpp>
#include <atomic>
#include <chrono>
#include <nlohmann/json.hpp>

export module common.metrics.request;

export namespace common {

  /**
   * @brief 请求相关的指标
   *
   */
  struct request_metrics_t {
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

  /**
   * @brief 请求指标监控
   *
   */
  auto start_request_metrics() -> asio::awaitable<void>;

  /**
   * @brief 获取请求指标数据
   *
   */
  auto get_request_metrics() -> nlohmann::json;

  /**
   * @brief 请求结束时的信息
   *
   * @param success     请求是否成功
   */
  struct request_end_info {
    bool success;
  };

  /**
   * @brief 接受新请求时调用
   *
   */
  auto push_one_request() -> std::chrono::steady_clock::time_point;

  /**
   * @brief 请求处理完成后调用
   *
   */
  auto pop_one_request(std::chrono::steady_clock::time_point begin_time, request_end_info info) -> void;

  /**
   * @brief 接受新连接时调用
   *
   */
  auto push_one_connection() -> void;

  /**
   * @brief 连接断开时调用
   *
   */
  auto pop_one_connection() -> void;

} // namespace common