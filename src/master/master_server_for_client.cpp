module;

#include "common/log.h"
#include "proto/proto.pb.h"
#include <asio.hpp>
#include <map>
#include <memory>
#include <mutex>
#include <set>

module master.server_for_client;

namespace master {

  auto sm_regist_handle(common::proto_frame_ptr request, common::connection_ptr conn) -> asio::awaitable<bool> {
    auto request_data = proto::sm_regist_request{};
    if (!request_data.ParseFromArray(request->data, request->data_len)) {
      LOG_ERROR("failed to parse sm_regist_request");
      co_await conn->send_response({.stat = 1}, *request);
      co_return false;
    }

    if (request_data.master_magic() != master_config.server.magic) {
      LOG_ERROR("invalid master magic {}", request_data.master_magic());
      co_await conn->send_response({.stat = 2}, *request);
      co_return false;
    }

    if (storage_registed(request_data.s_info().id())) {
      LOG_ERROR(std::format("storage {} has registed", request_data.s_info().id()));
      co_await conn->send_response({.stat = 3}, *request);
      co_return false;
    }

    LOG_INFO("storage regist {{ id: {}, magic: {}, ip: {}, port: {}, }}",
             request_data.s_info().id(), request_data.s_info().magic(),
             request_data.s_info().ip(), request_data.s_info().port());

    /* 响应同组 storage */
    auto response_data = proto::sm_regist_response{};
    response_data.set_group_id(group_which_storage_belongs(request_data.s_info().id()));
    for (auto storage : group_members_of_storage(request_data.s_info().id())) {
      auto s_info = response_data.add_s_infos();
      s_info->set_id(storage->get_data<storage_id_t>(conn_data::storage_id).value());
      s_info->set_magic(storage->get_data<storage_magic_t>(conn_data::storage_magic).value());
      s_info->set_port(storage->get_data<storage_port_t>(conn_data::storage_port).value());
      s_info->set_ip(storage->get_data<storage_ip_t>(conn_data::storage_ip).value());
    }
    auto response = common::create_frame(request->cmd, common::frame_type::response, response_data.ByteSizeLong());
    if (auto ok = co_await conn->send_response(response.get(), *request); !ok) {
      co_return false;
    }

    /* 保存 storage 信息 */
    conn->set_data<storage_id_t>(conn_data::storage_id, request_data.s_info().id());
    conn->set_data<storage_magic_t>(conn_data::storage_magic, request_data.s_info().magic());
    conn->set_data<storage_port_t>(conn_data::storage_port, request_data.s_info().port());
    conn->set_data<storage_ip_t>(conn_data::storage_ip, request_data.s_info().ip());
    regist_storage(conn);

    co_return true;
  }

  auto cm_fetch_one_storage_handle(common::proto_frame_ptr request, common::connection_ptr conn) -> asio::awaitable<bool> {
    auto need_space = common::ntohll(*(uint64_t *)request_recved->data);
    LOG_DEBUG(std::format("client fetch on storge for space {}", need_space));

    /* 获取合适的 storage */
    auto storage = common::connection_ptr{};
    for (auto i = 0uz; i < storage_conns.size(); ++i) {
      storage = next_storage();
      if (storage->get_data<uint64_t>(s_conn_data::storage_free_space) > need_space * 2) {
        break;
      }
      storage = nullptr;
    }

    if (!storage) {
      co_await conn->send_response(common::proto_frame{.stat = 1}, *request_recved);
      LOG_ERROR(std::format("no valid storage for space {}", need_space));
      co_return false;
    }

    auto response_data_to_send = proto::cm_fetch_one_storage_response{};
    response_data_to_send.mutable_s_info()->set_id(storage->get_data<uint32_t>(s_conn_data::storage_id).value());
    response_data_to_send.mutable_s_info()->set_magic(storage->get_data<uint32_t>(s_conn_data::storage_magic).value());
    response_data_to_send.mutable_s_info()->set_port(storage->get_data<uint16_t>(s_conn_data::storage_port).value());
    response_data_to_send.mutable_s_info()->set_ip(storage->get_data<std::string>(s_conn_data::storage_ip).value());
    auto response_to_send = (common::proto_frame *)malloc(sizeof(common::proto_frame) + response_data_to_send.ByteSizeLong());
    *response_to_send = common::proto_frame{.data_len = (uint32_t)response_data_to_send.ByteSizeLong()};
    response_data_to_send.SerializeToArray(response_to_send->data, response_to_send->data_len);
    co_await conn->send_response(response_to_send, *request_recved);
    free(response_to_send);
    co_return true;
  }

  auto regist_client(std::shared_ptr<common::connection> conn) -> void {
    conn->set_data<conn_type_t>(conn_data::conn_type, conn_type_t::client);
    auto lock = std::unique_lock{client_conns_lock};
    client_conns.emplace(conn);
  }

  auto unregist_client(std::shared_ptr<common::connection> conn) -> void {
    auto lock = std::unique_lock{client_conns_lock};
    client_conns.erase(conn);
  }

  auto request_from_client(std::shared_ptr<common::proto_frame> request, std::shared_ptr<common::connection> conn) -> asio::awaitable<bool> {
    if (auto it = request_handles.find(request->cmd); it != request_handles.end()) {
      co_return co_await it->second(request, conn);
    }
    LOG_ERROR("invalid request {} from client {}", *request, conn->address());
    co_return false;
  }

} // namespace master