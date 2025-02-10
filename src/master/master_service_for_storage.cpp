#include "./master_service_global.h"

// /* 定期更新 storage 可用空间 */
// static auto update_storage_free_disk_impl(std::shared_ptr<connection> conn) -> asio::awaitable<bool> {
//     auto id = co_await conn->send_req_frame(proto_frame_t{.cmd = (uint8_t)proto_cmd_e::ms_fs_free_size});
//     if (!id) {
//         co_return false;
//     }

//     auto res_frame = co_await conn->recv_res_frame(id.value());
//     if (!res_frame) {
//         co_return false;
//     }

//     auto useable_disk = *(uint64_t *)res_frame->data;
//     // g_log->log_debug(std::format("storage {} max useable disk {}", conn->to_string(), useable_disk));
//     conn->set_data(conn_data::s_free_disk, useable_disk);
//     co_return true;
// }

// static auto update_storage_free_disk(std::shared_ptr<connection> conn) -> asio::awaitable<void> {
//     while (co_await update_storage_free_disk_impl(conn)) {
//         co_await co_sleep_for(std::chrono::seconds{5});
//     }
// }

// auto on_storage_disconnect(std::shared_ptr<connection> conn) -> asio::awaitable<void> {
//     auto id = conn->get_data<uint32_t>(conn_data::s_id).value();
//     unregist_storage(id);

//     auto ip = conn->get_data<std::string>(conn_data::s_ip);
//     auto port = conn->get_data<uint32_t>(conn_data::s_port);
//     if (!ip.has_value() || !port.has_value()) {
//         g_m_log->log_fatal("failed to get storage ip or port");
//         exit(-1);
//     }
//     g_m_log->log_warn(std::format("storage {}:{} disconnect", ip.value(), port.value()));

//     co_return;
// }

auto on_storage_disconnect(std::shared_ptr<common::connection> conn) -> asio::awaitable<void> {
  co_return;
}

auto sm_regist_handle(REQUEST_HANDLE_PARAM) -> asio::awaitable<void> {
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
  conn->set_data<uint8_t>(conn_data::type, conn_type_storage);
  regist_storage(request_data.s_info().id(), conn);
}

auto recv_from_storage(REQUEST_HANDLE_PARAM) -> asio::awaitable<void> {
  co_return;
}

// std::map<uint8_t, req_handle_t> storage_req_handles{};

// auto recv_from_storage(std::shared_ptr<connection> conn) -> asio::awaitable<void> {
//     asio::co_spawn(co_await asio::this_coro::executor, update_storage_free_disk(conn), asio::detached);
//     while (true) {
//         auto req_frame = co_await conn->recv_req_frame();
//         if (!req_frame) {
//             co_await on_storage_disconnect(conn);
//             co_return;
//         }

//         if (storage_req_handles.contains(req_frame->cmd)) {
//             co_await storage_req_handles[req_frame->cmd](conn, req_frame);
//         } else {
//             g_m_log->log_error(std::format("unknown storage cmd {}", req_frame->cmd));
//         }
//     }
// }
