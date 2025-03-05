#pragma once
#include <asio.hpp>
#include <nlohmann/json.hpp>

namespace metrics {

/**
 * @brief 拓展监控指标
 *
 */
using metrics_extension = std::function<nlohmann::json()>;

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

/**
 * @brief 获取监控信息
 *
 * @return std::string
 */
auto get_metrics_as_string() -> std::string;

/**
 * @brief 监控服务
 *
 */
auto metrics_service(const nlohmann::json &json, std::vector<std::pair<std::string, metrics_extension>> exts) -> asio::awaitable<void>;

} // namespace metrics
