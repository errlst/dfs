#pragma once

#include "server_for_storage.h" // IWYU pragma: keep
#include <set>

namespace master_detail
{

  using namespace master;

  inline auto client_conns = std::set<common::connection_ptr>{};

  inline auto client_conns_lock = std::mutex{};

  auto sm_regist_handle(common::proto_frame_ptr request, common::connection_ptr conn) -> asio::awaitable<bool>;

  auto cm_fetch_one_storage_handle(common::proto_frame_ptr request, common::connection_ptr conn) -> asio::awaitable<bool>;

  auto cm_fetch_group_storages_handle(common::proto_frame_ptr request, common::connection_ptr conn) -> asio::awaitable<bool>;

  inline auto client_request_handles = std::map<common::proto_cmd, request_handle_t>{
      {common::proto_cmd::sm_regist, sm_regist_handle},
      {common::proto_cmd::cm_fetch_one_storage, cm_fetch_one_storage_handle},
      {common::proto_cmd::cm_fetch_group_storages, cm_fetch_group_storages_handle},
  };
} // namespace master_detail

namespace master
{

  /**
   * @brief 注册 client
   *
   */
  auto regist_client(std::shared_ptr<common::connection> conn) -> void;

  /**
   * @brief 注销 client
   *
   */
  auto unregist_client(std::shared_ptr<common::connection> conn) -> void;

  /**
   * @brief 处理 client 消息
   *
   */
  auto request_from_client(std::shared_ptr<common::proto_frame> request, std::shared_ptr<common::connection> conn) -> asio::awaitable<bool>;

  /**
   * @brief client 离线
   *
   */
  auto on_client_disconnect(common::connection_ptr conn) -> asio::awaitable<void>;

} // namespace master
