#pragma once

#include "server_util.h"
#include <proto.pb.h>
#include <set>

namespace storage_detail {

  using namespace storage;

  auto ss_regist_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool>;

  auto ss_upload_sync_start_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool>;

  auto ss_upload_sync_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool>;

  inline auto storage_conns = std::set<std::shared_ptr<common::connection>>{};

  inline auto storage_conns_mut = std::mutex{};

  inline auto storage_request_handles = std::map<common::proto_cmd, request_handle_t>{
      {common::proto_cmd::ss_upload_sync_start, ss_upload_sync_start_handle},
      {common::proto_cmd::ss_upload_sync, ss_upload_sync_handle},
  };

  /**
   * @brief 注册到其它 storage
   *
   */
  auto regist_to_storages(const proto::sm_regist_response &info) -> asio::awaitable<void>;

} // namespace storage_detail

namespace storage {

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
   * @brief 获取 storage
   *
   */
  auto registed_storages() -> std::set<std::shared_ptr<common::connection>>;

  /**
   * @brief storage 离线
   *
   */
  auto on_storage_disconnect(common::connection_ptr conn) -> asio::awaitable<void>;

  /**
   * @brief storage 消息
   *
   */
  auto request_from_storage(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool>;

} // namespace storage