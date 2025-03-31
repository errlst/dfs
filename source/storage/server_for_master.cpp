#include "server_for_master.h"
#include "config.h"
#include "server.h"
#include "server_for_storage.h"
#include "store_util.h"
#include <common/metrics.h>
#include <common/util.h>
#include <proto.pb.h>

namespace storage_detail {

  using namespace storage;

  auto ms_get_max_free_space_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool> {
    auto response = std::shared_ptr<common::proto_frame>{(common::proto_frame *)malloc(sizeof(common::proto_frame) + sizeof(uint64_t)), free};
    *response = {.data_len = sizeof(uint64_t)};
    *(uint64_t *)response->data = common::ntohll(hot_store_group()->max_free_space());
    co_await conn->send_response(response, *request);
    co_return true;
  }

  auto ms_get_metrics_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool> {
    auto s = common::get_metrics().dump();
    auto response_to_send = std::shared_ptr<common::proto_frame>{(common::proto_frame *)malloc(sizeof(common::proto_frame) + s.size()), free};
    *response_to_send = {.data_len = (uint32_t)s.size()};
    std::copy(s.begin(), s.end(), response_to_send->data);
    co_await conn->send_response(response_to_send, *request);
    co_return true;
  }

} // namespace storage_detail

namespace storage {

  using namespace storage_detail;

  auto regist_to_master() -> asio::awaitable<void> {
    for (auto i = 1;; i += 2) {
      if (i == 32) {
        exit(-1);
      }

      LOG_INFO("try connect to master {}:{}", storage_config.server.master_ip, storage_config.server.master_port);
      master_conn_ = co_await common::connection::connect_to(storage_config.server.master_ip, storage_config.server.master_port);
      if (master_conn_) {
        master_conn_->start(request_from_connection);
        master_conn_->set_data<conn_type_t>(conn_data::conn_type, conn_type_t::master);
        break;
      }
      LOG_ERROR(std::format("connect to master {}:{} failed", storage_config.server.master_ip, storage_config.server.master_port));
      auto timer = asio::steady_timer{co_await asio::this_coro::executor, std::chrono::seconds{i}};
      co_await timer.async_wait(asio::as_tuple(asio::use_awaitable));
    }
    LOG_INFO(std::format("connect to master suc"));

    /* 注册到 master */
    auto request_data = proto::sm_regist_request{};
    request_data.set_master_magic(storage_config.server.master_magic);
    request_data.mutable_s_info()->set_id(storage_config.server.id);
    request_data.mutable_s_info()->set_magic(storage_config.server.internal.storage_magic);
    request_data.mutable_s_info()->set_port(storage_config.server.port);
    request_data.mutable_s_info()->set_ip("127.0.0.1");

    auto request = common::create_frame(common::proto_cmd::sm_regist, common::frame_type::request, request_data.ByteSizeLong());
    request_data.SerializeToArray(request->data, request->data_len);
    auto response = co_await master_conn()->send_request_and_wait_response(request);
    if (!response || response->stat != 0) {
      LOG_ERROR("regist to master failed {}", response ? response->stat : -1);
      exit(-1);
    }

    auto response_data = proto::sm_regist_response{};
    if (!response_data.ParseFromArray(response->data, response->data_len)) {
      LOG_ERROR("failed to parse sm_regist_response");
      exit(-1);
    }

    co_await regist_to_storages(response_data);
  }

  auto master_conn() -> common::connection_ptr {
    return master_conn_;
  }

  auto on_master_disconnect(common::connection_ptr conn) -> asio::awaitable<void> {
    LOG_CRITICAL("master {} disconnected", conn->address());
    exit(-1);
  }

  auto request_from_master(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool> {
    if (auto it = master_request_handles.find(request->cmd); it != master_request_handles.end()) {
      co_return co_await it->second(request, conn);
    }
    LOG_ERROR("invalid request {} from master {}", *request, conn->address());
    co_return false;
  }

} // namespace storage