#pragma once

#include "server_for_storage.h"
#include <set>

namespace storage_detail {

  using namespace storage;

  auto cs_upload_start_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool>;

  auto cs_upload_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool>;

  auto cs_download_start_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool>;

  auto cs_download_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool>;

  inline auto client_request_handles = std::map<common::proto_cmd, request_handle_t>{
      {common::proto_cmd::ss_regist, ss_regist_handle},
      {common::proto_cmd::cs_upload_start, cs_upload_start_handle},
      {common::proto_cmd::cs_upload, cs_upload_handle},
      {common::proto_cmd::cs_download_start, cs_download_start_handle},
      {common::proto_cmd::cs_download, cs_download_handle},
  };

  inline auto client_conns = std::set<std::shared_ptr<common::connection>>{};

  inline auto client_conns_mut = std::mutex{};

} // namespace storage_detail

namespace storage {

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
   * @brief client 离线
   *
   */
  auto on_client_disconnect(common::connection_ptr conn) -> asio::awaitable<void>;

  /**
   * @brief client 请求
   *
   */
  auto request_from_client(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool>;

} // namespace storage