#pragma once
#include <asio.hpp>
#include <map>

namespace storage_detail {

  inline auto hot_file_atime_or_ctime = std::map<std::string, uint64_t>{};

  inline auto hot_file_atime_or_ctime_mut = std::mutex{};

  inline auto cold_file_access_times = std::map<std::string, uint64_t>{};

  inline auto cold_file_access_times_mut = std::mutex{};

  inline auto migrate_service_timer = std::unique_ptr<asio::steady_timer>{};

  /**
   * @brief 冷热数据迁移服务
   *
   */
  auto migrate_service() -> asio::awaitable<void>;

  /**
   * @brief 初始化冷数据迁移
   *
   */
  auto init_migrate_to_cold() -> void;

  /**
   * @brief 冷数据迁移服务
   *
   */
  auto migrate_to_cold_service() -> asio::awaitable<void>;

  /**
   * @brief 迁移单个冷数据
   *
   */
  auto migrate_to_cold_once(std::string_view abs_path) -> asio::awaitable<void>;

  /**
   * @brief 初始化热数据迁移
   *
   */
  auto init_migrate_to_hot() -> void;

  /**
   * @brief 热数据迁移服务
   *
   */
  auto migrate_to_hot_service() -> asio::awaitable<void>;

  /**
   * @brief 迁移单个热数据
   *
   */
  auto migrate_to_hot_once(std::string_view abs_path) -> asio::awaitable<void>;

} // namespace storage_detail

namespace storage {

  /**
   * @brief 初始化迁移服务
   *
   */
  auto start_migrate_service() -> asio::awaitable<void>;

  /**
   * @brief 手动进行数据迁移
   *
   */
  auto trigger_migrate_service() -> void;

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

} // namespace storage
