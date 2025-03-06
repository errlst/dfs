#pragma once
#include "./storage_service.h"
#include "./storage_service_handles.h"

namespace migrate_service {

/**
 * @brief 冷热数据迁移服务
 *
 * @param json
 * @return asio::awaitable<void>
 */
auto migrate_service(const nlohmann::json &json) -> asio::awaitable<void>;

/**
 * @brief 上传文件后调用
 *
 */
auto new_hot_file(const std::string &abs_path) -> void;

/**
 * @brief 访问热数据后调用
 *
 */
auto access_hot_file(const std::string &abs_path) -> void;

/**
 * @brief 修改热数据后调用
 *
 */
auto update_hot_file(const std::string &abs_path) -> void;

/**
 * @brief 访问冷数据后调用
 *
 */
auto access_cold_file(const std::string &abs_path) -> void;

} // namespace migrate_service
