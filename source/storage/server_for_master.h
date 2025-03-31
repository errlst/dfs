#pragma once
#include "server_util.h"

namespace storage_detail {

  using namespace storage;

  auto ms_get_max_free_space_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool>;

  auto ms_get_metrics_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool>;

  inline auto master_request_handles = std::map<common::proto_cmd, request_handle_t>{
      {common::proto_cmd::ms_get_max_free_space, ms_get_max_free_space_handle},
      {common::proto_cmd::ms_get_metrics, ms_get_metrics_handle},
  };

  inline auto master_conn_ = common::connection_ptr{};

} // namespace storage_detail

namespace storage {

  /**
   * @brief 注册到 master
   *
   */
  auto regist_to_master() -> asio::awaitable<void>;

  /**
   * @brief master 连接
   *
   */
  auto master_conn() -> common::connection_ptr;

  /**
   * @brief master 离线
   *
   */
  auto on_master_disconnect(common::connection_ptr conn) -> asio::awaitable<void>;

  /**
   * @brief master 请求
   *
   */
  auto request_from_master(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool>;

} // namespace storage