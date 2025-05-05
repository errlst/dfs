#include "server_for_storage.h"
#include "config.h"
#include "migrate.h"
#include "server.h"
#include "server_for_client.h"
#include "store_util.h"
#include <common/connection.h>
#include <common/util.h>
#include <proto.pb.h>

namespace storage_detail
{

  using namespace storage;

  auto ss_regist_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool>
  {
    auto request_data_recved = proto::ss_regist_request{};
    if (!request_data_recved.ParseFromArray(request->data, request->data_len))
    {
      LOG_ERROR(std::format("parse ss_regist_request failed"));
      co_await conn->send_response(common::proto_frame{.stat = 1}, *request);
      co_return false;
    }

    if (request_data_recved.master_magic() != storage_config.server.master_magic ||
        request_data_recved.storage_magic() != storage_config.server.internal.storage_magic)
    {
      LOG_ERROR(std::format("ss_regist request invalid magic, master magic 0x{:X}/0x{:X}, storage_magic 0x{:X}/0x{:X}",
                            request_data_recved.master_magic(), storage_config.server.master_magic,
                            request_data_recved.storage_magic(), storage_config.server.internal.storage_magic));
      co_await conn->send_response(common::proto_frame{.stat = 2}, *request);
      co_return false;
    }

    co_await conn->send_response(common::proto_frame{.stat = 0}, *request);
    regist_storage(conn);
    LOG_INFO(std::format("storage regist suc {}:{}", conn->ip(), conn->port()));
    co_return true;
  }

  auto ss_upload_sync_start_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool>
  {
    if (conn->has_data(conn_data::storage_sync_upload_file_id))
    {
      LOG_ERROR("storage already request sync upload yield");
      co_await conn->send_response(common::proto_frame{.stat = 1}, *request);
      co_return false;
    }

    if (request->data_len <= sizeof(uint64_t))
    {
      LOG_ERROR("ss_upload_sync_start request data_len invalid");
      co_await conn->send_response(common::proto_frame{.stat = 2}, *request);
      co_return false;
    }

    auto rel_path = std::string_view{request->data + sizeof(uint64_t), request->data_len - sizeof(uint64_t)};
    auto file_id = hot_store_group()->create_file(common::ntohll(*(uint64_t *)request->data), rel_path);
    if (!file_id)
    {
      LOG_ERROR(std::format("create file '{}' failed", rel_path));
      co_await conn->send_response(common::proto_frame{.stat = 3}, *request);
      co_return false;
    }

    conn->set_data<storage_sync_upload_file_id_t>(conn_data::storage_sync_upload_file_id, file_id.value());
    co_await conn->send_response(common::proto_frame{.stat = 0}, *request);
    co_return true;
  }

  auto ss_upload_sync_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool>
  {
    auto file_id = conn->get_data<storage_sync_upload_file_id_t>(conn_data::storage_sync_upload_file_id);
    if (!file_id.has_value())
    {
      LOG_ERROR("storage not request sync upload yield");
      co_await conn->send_response(common::proto_frame{.stat = 1}, *request);
      co_return false;
    }

    /* 结束同步 */
    if (request->data_len == 0 || request->stat == 255)
    {
      conn->del_data(conn_data::storage_sync_upload_file_id);

      auto res = hot_store_group()->close_write_file(file_id.value());
      if (!res)
      {
        co_await conn->send_response(common::proto_frame{.stat = 2}, *request);
        co_return false;
      }
      const auto &[root_path, rel_path] = res.value();

      co_await conn->send_response(common::proto_frame{.stat = 0}, *request);
      new_hot_file(std::format("{}/{}", root_path, rel_path));
      LOG_INFO("sync file {} suc from {}", rel_path, conn->address());
      co_return true;
    }

    if (!hot_store_group()->write_file(file_id.value(), std::span{request->data, request->data_len}))
    {
      co_await conn->send_response(common::proto_frame{.stat = 3}, *request);
      conn->del_data(conn_data::storage_sync_upload_file_id);
      co_return false;
    }

    co_await conn->send_response(common::proto_frame{.stat = 0}, *request);
    co_return true;
  }

  auto regist_to_storages(const proto::sm_regist_response &info) -> asio::awaitable<void>
  {
    for (const auto &s_info : info.s_infos())
    {
      auto s_conn = co_await common::connection::connect_to(s_info.ip(), s_info.port());
      if (!s_conn)
      {
        LOG_ERROR(std::format("connect to storage {}:{} failed", s_info.ip(), s_info.port()));
        continue;
      }
      s_conn->start(request_from_connection);
      LOG_INFO(std::format("connect to storage {}:{} suc", s_info.ip(), s_info.port()));

      /* 注册到其它 storage */
      auto request_data = proto::ss_regist_request{};
      request_data.set_master_magic(storage_config.server.master_magic);
      request_data.set_storage_magic(s_info.magic());

      auto request = common::create_frame(common::proto_cmd::ss_regist, common::frame_type::request, request_data.ByteSizeLong());
      request_data.SerializeToArray(request->data, request->data_len);
      auto response = co_await s_conn->send_request_and_wait_response(request);
      if (!response || response->stat != 0)
      {
        LOG_ERROR("regist to storage {}:{} failed {}, with master magic 0x{:X}, storage magic 0x{:X}",
                  s_info.ip(), s_info.port(), response ? response->stat : -1, storage_config.server.master_magic,
                  s_info.magic());
        continue;

        regist_storage(s_conn);
        LOG_INFO(std::format("regist to storage {}:{} suc", s_info.ip(), s_info.port()));
      }
    }
  }

} // namespace storage_detail

namespace storage
{

  using namespace storage_detail;

  auto regist_storage(std::shared_ptr<common::connection> conn) -> void
  {
    unregist_client(conn);
    conn->set_data<conn_type_t>(conn_data::conn_type, conn_type_t::storage);
    auto lock = std::unique_lock{storage_conns_mut};
    storage_conns.emplace(conn);
  }

  auto unregist_storage(std::shared_ptr<common::connection> conn) -> void
  {
    auto lock = std::unique_lock{storage_conns_mut};
    storage_conns.erase(conn);
  }

  auto registed_storages() -> std::set<std::shared_ptr<common::connection>>
  {
    auto lock = std::unique_lock{storage_conns_mut};
    return storage_conns;
  }

  auto on_storage_disconnect(std::shared_ptr<common::connection> conn) -> asio::awaitable<void>
  {
    unregist_storage(conn);
    LOG_ERROR("storage {} disconnect", conn->address());
    co_return;
  }

  auto request_from_storage(std::shared_ptr<common::proto_frame> request, std::shared_ptr<common::connection> conn) -> asio::awaitable<bool>
  {
    auto it = storage_request_handles.find(request->cmd);
    if (it != storage_request_handles.end())
    {
      co_return co_await it->second(request, conn);
    }
    LOG_ERROR("unknown request {} from storage {}", *request, conn->address());
    co_return false;
  }

} // namespace storage
