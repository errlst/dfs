module;

#include <asio.hpp>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

export module master.server_for_storage;

import common;
import master.server_util;
import master.config;

namespace master {

  auto storage_conns = std::map<storage_id_t, std::shared_ptr<common::connection>>{};

  auto storage_conns_lock = std::mutex{};

  auto storage_conns_vec = std::vector<std::shared_ptr<common::connection>>{};

} // namespace master

export namespace master {

  /**
   * @brief 获取 storage 的最大可用空间
   *
   */
  auto start_request_storage_max_free_space(common::connection_ptr conn) -> asio::awaitable<void> {

    co_return;
  }

  /**
   * @brief 停止获取
   *
   */
  auto stop_request_storage_max_free_space(common::connection_ptr conn) -> asio::awaitable<void> {
    co_return;
  }

  /**
   * @brief 注册 storage
   *
   */
  auto regist_storage(std::shared_ptr<common::connection> conn) -> void;

  /**
   * @brief 注销 storage
   *
   */
  auto unregist_storage(std::shared_ptr<common::connection> conn) -> void;

  /**
   * @brief 获取 storage vector
   *
   */
  auto storage_vec() -> std::vector<std::shared_ptr<common::connection>>;

  /**
   * @brief 判断 storage 是否已注册
   *
   */
  auto storage_registed(storage_id_t id) -> bool;

  /**
   * @brief 循环赛获取一个 storage
   *
   */
  auto one_storage_round_robin() -> std::shared_ptr<common::connection>;

  /**
   * @brief 获取组中的所有 storage
   *
   */
  auto storages_of_group(uint32_t group) -> std::vector<std::shared_ptr<common::connection>>;

  /**
   * @brief 计算 storage 所属的组
   *
   */
  auto group_which_storage_belongs(storage_id_t id) -> uint32_t { return (id - 1) / master_config.server.group_size + 1; }

  /**
   * @brief 某个 storage 的所有组员（包括自己）
   *
   */
  auto group_members_of_storage(storage_id_t id) -> std::vector<std::shared_ptr<common::connection>> { return storages_of_group(group_which_storage_belongs(id)); }

} // namespace master