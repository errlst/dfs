#pragma once
#include "../common/metrics_service.h"
#include <asio.hpp>

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