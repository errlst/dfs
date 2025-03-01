#pragma once
#include "./storage_service.h"
#include "./storage_service_handles.h"

namespace migrate_service {

auto start_migrate_service(const nlohmann::json &json) -> asio::awaitable<void>;

/* 新增文件 */
auto new_hot_file(const std::string &file) -> void;

/* 修改或者访问热数据 */
auto access_hot_file(const std::string &file) -> void;

/* 修改或者访问冷数据 */
auto access_cold_file(const std::string &file) -> void;

} // namespace migrate_service
