#include "./storage_service_handles.h"
#include "../common/util.h"
#include "../proto/proto.pb.h"

auto ss_regist_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool> {
  auto request_data_recved = proto::ss_regist_request{};
  if (!request_data_recved.ParseFromArray(request_recved->data, request_recved->data_len)) {
    LOG_ERROR(std::format("parse ss_regist_request failed"));
    co_await conn->send_response(common::proto_frame{.stat = 1}, request_recved);
    co_return false;
  }

  if (request_data_recved.master_magic() != ss_config.master_magic ||
      request_data_recved.storage_magic() != ss_config.storage_magic) {
    LOG_ERROR(std::format("ss_regist request invalid magic, master: {}, storage: {}",
                          request_data_recved.master_magic(), request_data_recved.storage_magic()));
    co_await conn->send_response(common::proto_frame{.stat = 2}, request_recved);
    co_return false;
  }

  co_await conn->send_response(common::proto_frame{.stat = 0}, request_recved);
  conn->set_data<uint8_t>(conn_data::type, CONN_TYPE_STORAGE);
  regist_storage(conn);
  LOG_INFO(std::format("storage regist suc {}:{}", conn->ip(), conn->port()));
  co_return true;
}

auto ss_upload_sync_open_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool> {
  if (conn->has_data(conn_data::storage_sync_upload_file_id)) {
    LOG_ERROR("storage already request sync upload yield");
    co_await conn->send_response(common::proto_frame{.stat = 1}, request_recved);
    co_return false;
  }

  if (request_recved->data_len <= sizeof(uint64_t)) {
    LOG_ERROR("ss_upload_sync_open request data_len invalid");
    co_await conn->send_response(common::proto_frame{.stat = 2}, request_recved);
    co_return false;
  }

  auto file_path = std::string_view{request_recved->data + sizeof(uint64_t), request_recved->data_len - sizeof(uint64_t)};
  auto file_id = hot_store_group->create_file(ntohll(*(uint64_t *)request_recved->data), file_path);
  if (!file_id) {
    LOG_ERROR(std::format("create file {} failed", file_path));
    co_await conn->send_response(common::proto_frame{.stat = 3}, request_recved);
    co_return false;
  }

  conn->set_data<uint64_t>(conn_data::storage_sync_upload_file_id, file_id.value());
  co_await conn->send_response(common::proto_frame{.stat = 0}, request_recved);
  co_return true;
}

auto ss_upload_sync_append_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool> {
  auto file_id = conn->get_data<uint64_t>(conn_data::storage_sync_upload_file_id);
  if (!file_id.has_value()) {
    LOG_ERROR("storage not request sync upload yield");
    co_await conn->send_response(common::proto_frame{.stat = 1}, request_recved);
    co_return false;
  }

  if (request_recved->data_len == 0) {
    LOG_INFO("sync upload file done");
    hot_store_group->close_file(file_id.value());
    conn->del_data(conn_data::storage_sync_upload_file_id);
    co_await conn->send_response(common::proto_frame{.stat = 0}, request_recved);
    co_return true;
  }

  if (!hot_store_group->write_file(file_id.value(), std::span{request_recved->data, request_recved->data_len})) {
    LOG_ERROR("write file failed");
    co_await conn->send_response(common::proto_frame{.stat = 2}, request_recved);
    conn->del_data(conn_data::storage_sync_upload_file_id);
    co_return false;
  }

  co_await conn->send_response(common::proto_frame{.stat = 0}, request_recved);
  co_return true;
}

auto ms_get_free_space_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool> {
  auto response_to_send = std::shared_ptr<common::proto_frame>{(common::proto_frame *)malloc(sizeof(common::proto_frame) + sizeof(uint64_t)), free};
  *response_to_send = {.data_len = sizeof(uint64_t)};
  *(uint64_t *)response_to_send->data = ntohll(hot_store_group->max_free_space());
  co_await conn->send_response(response_to_send.get(), request_recved);
  co_return true;
}

auto ms_get_metrics_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool> {
  auto s = metrics::get_metrics_as_string();
  auto response_to_send = std::shared_ptr<common::proto_frame>{(common::proto_frame *)malloc(sizeof(common::proto_frame) + s.size()), free};
  *response_to_send = {.data_len = (uint32_t)s.size()};
  std::copy(s.begin(), s.end(), response_to_send->data);
  co_await conn->send_response(response_to_send.get(), request_recved);
  co_return true;
}

auto cs_upload_open_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool> {
  if (conn->has_data(conn_data::client_upload_file_id)) {
    LOG_ERROR("client already request upload yield");
    co_await conn->send_response(common::proto_frame{.stat = 1}, request_recved);
    co_return false;
  }

  if (request_recved->data_len != sizeof(uint64_t)) {
    LOG_ERROR("cs_upload_open request data_len invalid");
    co_await conn->send_response(common::proto_frame{.stat = 2}, request_recved);
    co_return false;
  }

  auto file_size = ntohll(*(uint64_t *)request_recved->data);
  auto file_id = hot_store_group->create_file(file_size);
  if (!file_id) {
    LOG_ERROR(std::format("create file failed for file_size {}", file_size));
    co_await conn->send_response(common::proto_frame{.stat = 3}, request_recved);
    co_return false;
  }

  conn->set_data<uint64_t>(conn_data::client_upload_file_id, file_id.value());
  co_await conn->send_response(common::proto_frame{.stat = 0}, request_recved);
  co_return true;
}

auto cs_upload_append_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool> {
  auto file_id = conn->get_data<uint64_t>(conn_data::client_upload_file_id);
  if (!file_id) {
    LOG_ERROR("client not request upload yield");
    co_await conn->send_response(common::proto_frame{.stat = 1}, request_recved);
    co_return false;
  }

  if (request_recved->data_len == 0) {
    LOG_ERROR(std::format("client cancel upload"));
    co_await conn->send_response(common::proto_frame{.stat = 2}, request_recved);
    conn->del_data(conn_data::client_upload_file_id);
    co_return false;
  }

  if (!hot_store_group->write_file(file_id.value(), std::span{request_recved->data, request_recved->data_len})) {
    LOG_ERROR("write file failed");
    co_await conn->send_response(common::proto_frame{.stat = 3}, request_recved);
    conn->del_data(conn_data::client_upload_file_id);
    co_return false;
  }

  co_await conn->send_response(common::proto_frame{.stat = 0}, request_recved);
  co_return true;
}

auto cs_upload_close_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool> {
  auto file_id = conn->get_data<uint64_t>(conn_data::client_upload_file_id);
  if (!file_id) {
    LOG_ERROR("client not request upload yield");
    co_await conn->send_response(common::proto_frame{.stat = 1}, request_recved);
    co_return false;
  }

  auto file_path = hot_store_group->close_file(file_id.value(), std::string_view{request_recved->data, request_recved->data_len});
  if (!file_path) {
    LOG_ERROR("close file failed");
    co_await conn->send_response(common::proto_frame{.stat = 2}, request_recved);
    conn->del_data(conn_data::client_upload_file_id);
    co_return false;
  }

  {
    auto lock = std::unique_lock{unsync_uploaded_files_mut};
    unsync_uploaded_files.emplace(file_path.value());
  }

  file_path = std::format("{}/{}", ss_config.group_id, file_path.value());
  auto response_to_send = std::shared_ptr<common::proto_frame>{(common::proto_frame *)malloc(sizeof(common::proto_frame) + file_path->size()), free};
  *response_to_send = {.data_len = (uint32_t)file_path->size()};
  std::copy(file_path->begin(), file_path->end(), response_to_send->data);
  co_await conn->send_response(response_to_send.get(), request_recved);

  conn->del_data(conn_data::client_upload_file_id);
  co_return true;
}

auto cs_download_open_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool> {
  if (conn->has_data(conn_data::client_download_file_id)) {
    LOG_ERROR("client already request download yield");
    co_await conn->send_response(common::proto_frame{.stat = 1}, request_recved);
    co_return false;
  }

  auto valid_store_group = std::shared_ptr<store_ctx_group>{};
  auto filesize = 0ul;
  for (auto store_group : store_groups) {
    auto res = store_group->open_file(std::string_view{request_recved->data, request_recved->data_len});
    if (res.has_value()) {
      valid_store_group = store_group;
      filesize = res.value().second;
      conn->set_data<uint64_t>(conn_data::client_download_file_id, res.value().first);
      conn->set_data<std::shared_ptr<store_ctx_group>>(conn_data::client_download_store_group, store_group);
      break;
    }
  }
  if (!valid_store_group) {
    LOG_ERROR(std::format("not find file {}", std::string_view{request_recved->data, request_recved->data_len}));
    co_await conn->send_response(common::proto_frame{.stat = 2}, request_recved);
    co_return false;
  }

  auto response_to_send = std::shared_ptr<common::proto_frame>{(common::proto_frame *)malloc(sizeof(common::proto_frame) + sizeof(uint64_t)), free};
  *response_to_send = {.data_len = sizeof(uint64_t)};
  *(uint64_t *)response_to_send->data = htonll(filesize);
  co_await conn->send_response(response_to_send.get(), request_recved);
  co_return true;
}

auto cs_download_append_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool> {
  auto file_id = conn->get_data<uint64_t>(conn_data::client_download_file_id);
  if (!file_id) {
    LOG_ERROR("client not request download yield");
    co_await conn->send_response(common::proto_frame{.stat = 1}, request_recved);
    co_return false;
  }

  auto store_group = conn->get_data<std::shared_ptr<store_ctx_group>>(conn_data::client_download_store_group);
  if (!store_group) {
    LOG_ERROR("unknownd internal error");
    co_await conn->send_response(common::proto_frame{.stat = 2}, request_recved);
    co_return false;
  }

  auto response_to_send = std::shared_ptr<common::proto_frame>{(common::proto_frame *)malloc(sizeof(common::proto_frame) + 5_MB), free};
  *response_to_send = {
      .data_len = (uint32_t)store_group.value()->read_file(file_id.value(), response_to_send->data, 5_MB),
  };
  co_await conn->send_response(response_to_send.get(), request_recved);
  co_return true;
}
