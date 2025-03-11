/**
 * @file 同步上传文件
 * @author your name (you@domain.com)
 * @brief
 * @version 0.1
 * @date 2025-03-11
 *
 * @copyright Copyright (c) 2025
 *
 */

#include "common/connection.h" // IWYU pragma: keep
#include <generator>

/**
 * @brief 同步服务
 *
 */
auto sync_service() -> asio::awaitable<void>;

/**
 * @brief 手动触发同步服务
 *
 */
auto start_service() -> void;
/**
 * @brief 增加未同步文件
 *
 */
auto push_not_synced_file(std::string_view rel_path) -> void;

/**
 * @brief 遍历未同步文件，并移除
 *
 */
auto pop_not_synced_file() -> std::generator<std::string>;

/**
 * @brief 未同步文件数量
 *
 */
auto not_synced_file_count() -> size_t;