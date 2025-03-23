#pragma once
#include "storage_service.h" // IWYU pragma: keep

/**
 * @brief 冷热数据迁移服务
 *
 */
auto migrate_service() -> asio::awaitable<void>;

/**
 * @brief 初始化迁移服务
 *
 */
auto init_migrate_service() -> asio::awaitable<void>;

/**
 * @brief 手动进行数据迁移
 *
 */
auto start_migrate_service() -> void;

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
