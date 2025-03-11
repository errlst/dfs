#pragma once
#include "storage_service_handles.h" // IWYU pragma: keep

/**
 * @brief storage 性能监控
 *
 * @return nlohmann::json
 */
auto storage_metrics() -> nlohmann::json;

/**
 * @brief storage 服务
 *
 * @param json
 * @return asio::awaitable<void>
 */
auto storage_service(const nlohmann::json &json) -> asio::awaitable<void>;

/**
 * @brief 结束 storage 服务
 *
 */
auto over_storage_service() -> void;