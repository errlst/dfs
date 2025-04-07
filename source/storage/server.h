#pragma once
#include <common/connection.h>
#include <common/json.h>

namespace storage_detail
{
  /**
   * @brief
   *
   */
  auto storage_info_metrics() -> nlohmann::json;

  /**
   * @brief 处理请求
   *
   */
  auto request_from_connection(std::shared_ptr<common::proto_frame> request, std::shared_ptr<common::connection> conn) -> asio::awaitable<void>;

} // namespace storage_detail

namespace storage
{

  /**
   * @brief storage 服务
   *
   */
  auto storage_server() -> asio::awaitable<void>;

} // namespace storage
