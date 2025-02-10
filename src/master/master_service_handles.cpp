#include "./master_service_global.h"

/* 定期获取 storage 的可用空间 */
auto request_storage_free_space(std::shared_ptr<common::connection> conn) -> asio::awaitable<void> {
  while (true) {
    auto id = co_await conn->send_request(common::proto_frame{.cmd = common::proto_cmd::ms_get_free_space});
    if (!id) {
      co_return;
    }

    auto response_recved = co_await conn->recv_response(id.value());
    if (!response_recved) {
      co_return;
    }

    auto free_space = htonll(*(uint64_t *)response_recved->data);
    conn->set_data<uint64_t>(conn_data::storage_free_space, free_space);
    LOG_DEBUG(std::format("get storage free space {}", free_space));
    co_await co_sleep_for(std::chrono::seconds{100});
  }
}

auto sm_regist_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<void> {
  auto request_data = proto::sm_regist_request{};
  if (!request_data.ParseFromArray(request_recved->data, request_recved->data_len)) {
    LOG_ERROR(std::format("failed to parse sm_regist_request"));
    co_await conn->send_response(common::proto_frame{.stat = 1}, request_recved);
    co_return;
  }

  if (request_data.master_magic() != conf.master_magic) {
    LOG_ERROR(std::format("invalid master magic {}", request_data.master_magic()));
    co_await conn->send_response(common::proto_frame{.stat = 2}, request_recved);
    co_return;
  }

  if (storage_registed(request_data.s_info().id())) {
    LOG_ERROR(std::format("storage {} has registed", request_data.s_info().id()));
    co_await conn->send_response(common::proto_frame{.stat = 3}, request_recved);
    co_return;
  }

  LOG_INFO(std::format(R"(storage request regist{{
    id: {},
    magic: {},
    ip: {},
    port: {}, 
  }} )",
                       request_data.s_info().id(), request_data.s_info().magic(), request_data.s_info().ip(), request_data.s_info().port()));

  /* 响应同组 storage */
  auto response_data_to_send = proto::sm_regist_response{};
  response_data_to_send.set_group_id(storage_group(request_data.s_info().id()));
  for (auto storage : group_storages(storage_group(request_data.s_info().id()))) {
    auto s_info = response_data_to_send.add_s_infos();
    s_info->set_id(storage->get_data<uint32_t>(conn_data::storage_id).value());
    s_info->set_magic(storage->get_data<uint32_t>(conn_data::storage_magic).value());
    s_info->set_port(storage->get_data<uint16_t>(conn_data::storage_port).value());
    s_info->set_ip(storage->get_data<std::string>(conn_data::storage_ip).value());
  }
  auto response_to_send = (common::proto_frame *)malloc(sizeof(common::proto_frame) + response_data_to_send.ByteSizeLong());
  *response_to_send = {.data_len = (uint32_t)response_data_to_send.ByteSizeLong()};
  response_data_to_send.SerializeToArray(response_to_send->data, response_to_send->data_len);
  auto ok = co_await conn->send_response(response_to_send, request_recved);
  free(response_to_send);
  if (!ok) {
    co_return;
  }

  /* 保存 storage 信息 */
  conn->set_data<uint32_t>(conn_data::storage_id, request_data.s_info().id());
  conn->set_data<uint32_t>(conn_data::storage_magic, request_data.s_info().magic());
  conn->set_data<uint16_t>(conn_data::storage_port, request_data.s_info().port());
  conn->set_data<std::string>(conn_data::storage_ip, request_data.s_info().ip());
  conn->set_data<uint8_t>(conn_data::type, CONN_TYPE_STORAGE);
  regist_storage(request_data.s_info().id(), conn);
  conn->add_exetension_work(request_storage_free_space);
}

auto cm_fetch_one_storage_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<void> {
  auto need_space = ntohll(*(uint64_t *)request_recved->data);
  LOG_DEBUG(std::format("client fetch on storge for space {}", need_space));

  /* 获取合适的 storage */
  auto storage = std::shared_ptr<common::connection>{};
  for (auto i = 0; i < storage_conns.size(); ++i) {
    storage = next_storage();
    if (storage->get_data<uint64_t>(conn_data::storage_free_space) > need_space * 2) {
      break;
    }
    storage = nullptr;
  }

  if (!storage) {
    co_await conn->send_response(common::proto_frame{.stat = 1}, request_recved);
    LOG_ERROR(std::format("no valid storage for space {}", need_space));
    co_return;
  }

  auto response_data_to_send = proto::cm_fetch_one_storage_response{};
  response_data_to_send.mutable_s_info()->set_id(storage->get_data<uint32_t>(conn_data::storage_id).value());
  response_data_to_send.mutable_s_info()->set_magic(storage->get_data<uint32_t>(conn_data::storage_magic).value());
  response_data_to_send.mutable_s_info()->set_port(storage->get_data<uint16_t>(conn_data::storage_port).value());
  response_data_to_send.mutable_s_info()->set_ip(storage->get_data<std::string>(conn_data::storage_ip).value());
  auto response_to_send = (common::proto_frame *)malloc(sizeof(common::proto_frame) + response_data_to_send.ByteSizeLong());
  *response_to_send = common::proto_frame{.data_len = (uint32_t)response_data_to_send.ByteSizeLong()};
  response_data_to_send.SerializeToArray(response_to_send->data, response_to_send->data_len);
  co_await conn->send_response(response_to_send, request_recved);
  free(response_to_send);
}
