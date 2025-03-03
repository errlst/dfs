#pragma once
#include "./storage_service.h"
#include "./storage_service_handles.h"

namespace migrate_service {

auto start_migrate_service(const nlohmann::json &json) -> asio::awaitable<void>;

/* 新增文件 */

/**
 * @brief client 上传文件，或 storage 同步文件完成后调用
 *
 */
auto new_hot_file(const std::string &abs_path) -> void;

/* 修改或者访问热数据 */

/**
 * @brief client 访问文件或修改文件后调用
 *
 */
auto access_hot_file(const std::string &abs_path) -> void;

/**
 * @brief client 访问或修改文件后调用
 *
 */
auto access_cold_file(const std::string &abs_path) -> void;

} // namespace migrate_service
