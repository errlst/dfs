#pragma once

#include <common/connection.h>
#include <generator>
#include <queue>

namespace storage_detail {

  inline auto not_sync_files = std::queue<std::string>{};

  inline auto not_sync_files_bk = std::queue<std::string>{};

  inline auto not_synced_files_mut = std::mutex{};

  inline auto sync_service_timer = std::unique_ptr<asio::steady_timer>{};

  /**
   * @brief 同步服务
   *
   */
  auto do_sync_service() -> asio::awaitable<void>;

  /**
   * @brief 获取有效的可同步的 storages
   *
   */
  auto get_valid_syncable_storages(std::string_view rel_path, uint64_t file_id, uint64_t file_size, std::string_view abs_path) -> asio::awaitable<std::vector<std::shared_ptr<common::connection>>>;

  /**
   * @brief 普通方式同步一个文件
   *
   */
  auto sync_file_normal(std::string_view rel_path, uint64_t file_id, uint64_t file_size, std::string_view abs_path) -> asio::awaitable<bool>;

  /**
   * @brief 零拷贝方式同步一个文件
   *
   */
  auto sync_file_zero_copy(std::string_view rel_path, uint64_t file_id, uint64_t file_size, std::string_view abs_path) -> asio::awaitable<bool>;

  /**
   * @brief 遍历未同步文件，并移除
   *
   */
  auto pop_not_synced_files() -> std::generator<std::string>;

  /**
   * @brief 未同步文件数量
   *
   */
  auto not_synced_file_count() -> size_t;

} // namespace storage_detail

namespace storage {

  /**
   * @brief 开始同步
   *
   */
  auto start_sync_service() -> asio::awaitable<void>;

  /**
   * @brief 触发同步服务
   *
   */
  auto trigger_sync_service() -> void;

  /**
   * @brief 增加未同步文件
   *
   */
  auto push_not_synced_file(std::string_view rel_path) -> void;

} // namespace storage
