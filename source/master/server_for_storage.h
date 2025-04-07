#pragma once

#include "config.h"
#include "server_util.h"
#include <common/connection.h>
#include <common/json.h>
#include <common/protocol.h>

namespace master_detail
{

  using namespace master;

  inline auto storage_conns = std::map<storage_id_t, std::shared_ptr<common::connection>>{};

  inline auto storage_conns_lock = std::mutex{};

  inline auto storage_conns_vec = std::vector<std::shared_ptr<common::connection>>{};

  inline auto storage_metricses = std::map<common::connection_ptr, nlohmann::json>{};

  inline auto storage_metricses_lock = std::mutex{};

  /**
   * @brief 获取 storage 的最大可用空间
   *
   */
  auto request_storage_max_free_space(common::connection_ptr conn) -> asio::awaitable<void>;

  /**
   * @brief 获取 storage 的监控信息
   *
   */
  auto request_storage_metrics(common::connection_ptr conn) -> asio::awaitable<void>;

} // namespace master_detail

namespace master
{

  /**
   * @brief 获取 storage 的监控信息
   *
   */
  auto storage_metrics() -> nlohmann::json;

  /**
   * @brief storage 离线
   *
   */
  auto on_storage_disconnect(common::connection_ptr conn) -> asio::awaitable<void>;

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
  auto next_storage_round_robin() -> std::shared_ptr<common::connection>;

  /**
   * @brief 获取组中的所有 storage
   *
   */
  auto storages_of_group(uint32_t group) -> std::vector<std::shared_ptr<common::connection>>;

  /**
   * @brief 计算 storage 所属的组
   *
   */
  inline auto group_storage_belongs_to(storage_id_t id) -> uint32_t { return (id - 1) / master_config.server.group_size + 1; }

  /**
   * @brief 某个 storage 的所有组员（包括自己）
   *
   */
  inline auto group_members_of_storage(storage_id_t id) -> std::vector<std::shared_ptr<common::connection>> { return storages_of_group(group_storage_belongs_to(id)); }

} // namespace master