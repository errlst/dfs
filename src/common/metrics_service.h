#pragma once
#include <asio.hpp>
#include <nlohmann/json.hpp>

namespace metrics {

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
 * @brief 添加拓展监控
 *        最好通过 asio::co_spawn() 调用，因为内部会阻塞
 *
 */
auto add_metrics_extension(std::string name, std::function<nlohmann::json()> ext) -> asio::awaitable<void>;

/**
 * @brief 监控服务
 *
 */
auto metrics_service(const std::string &base_path) -> asio::awaitable<void>;

} // namespace metrics
