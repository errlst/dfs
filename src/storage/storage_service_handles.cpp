#include "./storage_service_handles.h"
#include "../common/util.h"

auto ss_regist_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<void> {
  auto request_data_recved = proto::ss_regist_request{};
  if (!request_data_recved.ParseFromArray(request_recved->data, request_recved->data_len)) {
    LOG_ERROR(std::format("parse ss_regist_request failed"));
    co_await conn->send_response(common::proto_frame{.stat = 1}, request_recved);
    co_return;
  }

  if (request_data_recved.master_magic() != ss_config.master_magic ||
      request_data_recved.storage_magic() != ss_config.storage_magic) {
    LOG_ERROR(std::format("ss_regist request invalid magic, master: {}, storage: {}",
                          request_data_recved.master_magic(), request_data_recved.storage_magic()));
    co_await conn->send_response(common::proto_frame{.stat = 2}, request_recved);
    co_return;
  }

  co_await conn->send_response(common::proto_frame{.stat = 0}, request_recved);
  conn->set_data<uint8_t>(conn_data::type, CONN_TYPE_STORAGE);
  regist_storage(conn);
  LOG_INFO(std::format("storage regist suc {}:{}", conn->ip(), conn->port()));
  co_return;
}

auto ss_upload_sync_open_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<void> {
  if (conn->has_data(conn_data::storage_sync_upload_file_id)) {
    LOG_ERROR("storage already request sync upload yield");
    co_await conn->send_response(common::proto_frame{.stat = 1}, request_recved);
    co_return;
  }

  if (request_recved->data_len <= sizeof(uint64_t)) {
    LOG_ERROR("ss_upload_sync_open request data_len invalid");
    co_await conn->send_response(common::proto_frame{.stat = 2}, request_recved);
    co_return;
  }

  auto file_path = std::string_view{request_recved->data + sizeof(uint64_t), request_recved->data_len - sizeof(uint64_t)};
  auto file_id = hot_stores->create_file(ntohll(*(uint64_t *)request_recved->data), file_path);
  if (!file_id) {
    LOG_ERROR(std::format("create file {} failed", file_path));
    co_await conn->send_response(common::proto_frame{.stat = 3}, request_recved);
    co_return;
  }

  conn->set_data<uint64_t>(conn_data::storage_sync_upload_file_id, file_id.value());
  co_await conn->send_response(common::proto_frame{.stat = 0}, request_recved);
}

auto ss_upload_sync_append_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<void> {
  auto file_id = conn->get_data<uint64_t>(conn_data::storage_sync_upload_file_id);
  if (!file_id.has_value()) {
    LOG_ERROR("storage not request sync upload yield");
    co_await conn->send_response(common::proto_frame{.stat = 1}, request_recved);
    co_return;
  }

  if (request_recved->data_len == 0) {
    LOG_INFO("sync upload file done");
    hot_stores->close_file(file_id.value());
    conn->del_data(conn_data::storage_sync_upload_file_id);
    co_await conn->send_response(common::proto_frame{.stat = 0}, request_recved);
    co_return;
  }

  if (!hot_stores->write_file(file_id.value(), std::span{request_recved->data, request_recved->data_len})) {
    LOG_ERROR("write file failed");
    co_await conn->send_response(common::proto_frame{.stat = 2}, request_recved);
    conn->del_data(conn_data::storage_sync_upload_file_id);
    co_return;
  }

  co_await conn->send_response(common::proto_frame{.stat = 0}, request_recved);
}

auto ms_get_free_space_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<void> {
  auto response_to_send = (common::proto_frame *)malloc(sizeof(common::proto_frame) + sizeof(uint64_t));
  *response_to_send = {.data_len = sizeof(uint64_t)};
  *(uint64_t *)response_to_send->data = ntohll(hot_stores->max_free_space());
  co_await conn->send_response(response_to_send, request_recved);
}

auto cs_upload_open_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<void> {
  if (conn->has_data(conn_data::client_upload_file_id)) {
    LOG_ERROR("client already request upload yield");
    co_await conn->send_response(common::proto_frame{.stat = 1}, request_recved);
    co_return;
  }

  if (request_recved->data_len != sizeof(uint64_t)) {
    LOG_ERROR("cs_upload_open request data_len invalid");
    co_await conn->send_response(common::proto_frame{.stat = 2}, request_recved);
    co_return;
  }

  auto file_size = ntohll(*(uint64_t *)request_recved->data);
  auto file_id = hot_stores->create_file(file_size);
  if (!file_id) {
    LOG_ERROR(std::format("create file failed for file_size {}", file_size));
    co_await conn->send_response(common::proto_frame{.stat = 3}, request_recved);
    co_return;
  }

  conn->set_data<uint64_t>(conn_data::client_upload_file_id, file_id.value());
  co_await conn->send_response(common::proto_frame{.stat = 0}, request_recved);
}

auto cs_upload_append_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<void> {
  auto file_id = conn->get_data<uint64_t>(conn_data::client_upload_file_id);
  if (!file_id) {
    LOG_ERROR("client not request upload yield");
    co_await conn->send_response(common::proto_frame{.stat = 1}, request_recved);
    co_return;
  }

  if (request_recved->data_len == 0) {
    LOG_ERROR(std::format("client cancel upload"));
    co_await conn->send_response(common::proto_frame{.stat = 2}, request_recved);
    conn->del_data(conn_data::client_upload_file_id);
    co_return;
  }

  if (!hot_stores->write_file(file_id.value(), std::span{request_recved->data, request_recved->data_len})) {
    LOG_ERROR("write file failed");
    co_await conn->send_response(common::proto_frame{.stat = 3}, request_recved);
    conn->del_data(conn_data::client_upload_file_id);
    co_return;
  }

  co_await conn->send_response(common::proto_frame{.stat = 0}, request_recved);
}

auto cs_upload_close_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<void> {
  auto file_id = conn->get_data<uint64_t>(conn_data::client_upload_file_id);
  if (!file_id) {
    LOG_ERROR("client not request upload yield");
    co_await conn->send_response(common::proto_frame{.stat = 1}, request_recved);
    co_return;
  }

  auto file_path = hot_stores->close_file(file_id.value(), std::string_view{request_recved->data, request_recved->data_len});
  if (!file_path) {
    LOG_ERROR("close file failed");
    co_await conn->send_response(common::proto_frame{.stat = 2}, request_recved);
    conn->del_data(conn_data::client_upload_file_id);
    co_return;
  }

  {
    auto lock = std::unique_lock{unsync_uploaded_files_mut};
    unsync_uploaded_files.emplace(file_path.value());
  }

  file_path = std::format("{}/{}", storage_group_id, file_path.value());
  auto response_to_send = (common::proto_frame *)malloc(sizeof(common::proto_frame) + file_path->size());
  *response_to_send = {.data_len = (uint32_t)file_path->size()};
  std::copy(file_path->begin(), file_path->end(), response_to_send->data);
  co_await conn->send_response(response_to_send, request_recved);
  free(response_to_send);

  conn->del_data(conn_data::client_upload_file_id);
}
