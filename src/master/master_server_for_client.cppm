module;

#include <asio.hpp>
#include <map>
#include <memory>
#include <mutex>
#include <set>

export module master.server_for_client;

import common;
import master.config;
import master.server_util;
import master.server_for_storage;

namespace master {

  auto client_conns = std::set<common::connection_ptr>{};

  auto client_conns_lock = std::mutex{};

  auto sm_regist_handle(common::proto_frame_ptr request, common::connection_ptr conn) -> asio::awaitable<bool>;

  auto cm_fetch_one_storage_handle(common::proto_frame_ptr request, common::connection_ptr conn) -> asio::awaitable<bool>;

  auto request_handles = std::map<common::proto_cmd, request_handle_t>{
      {common::proto_cmd::sm_regist, sm_regist_handle},
      {common::proto_cmd::cm_fetch_one_storage, cm_fetch_one_storage_handle},
  };

} // namespace master

export namespace master {

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

} // namespace master