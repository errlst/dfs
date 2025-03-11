#include "./storage_service_handles.h"
#include "../common/metrics_service.h"
#include "../common/util.h"
#include "../proto/proto.pb.h"
#include "migrate_service.h"
#include <queue>

static auto hot_store_group_ = std::shared_ptr<store_ctx_group>{};
static auto cold_store_group_ = std::shared_ptr<store_ctx_group>{};

auto init_store_group(const std::vector<std::string> &hot_paths, const std::vector<std::string> &cold_paths) -> void {
  hot_store_group_ = std::make_shared<store_ctx_group>("hot_store_group", hot_paths);
  cold_store_group_ = std::make_shared<store_ctx_group>("cold_store_group", cold_paths);
}

auto hot_store_group() -> std::shared_ptr<store_ctx_group> {
  return hot_store_group_;
}

auto is_hot_store_group(std::shared_ptr<store_ctx_group> store_group) -> bool {
  return store_group == hot_store_group_;
}

auto cold_store_group() -> std::shared_ptr<store_ctx_group> {
  return cold_store_group_;
}

auto iterate_store_groups() -> std::generator<std::shared_ptr<store_ctx_group>> {
  co_yield hot_store_group_;
  co_yield cold_store_group_;
}

/************************************************************************************************************** */
/************************************************************************************************************** */

static auto master_conn_ = std::shared_ptr<common::connection>{};

auto connected_to_master(std::shared_ptr<common::connection> conn) -> void {
  master_conn_ = conn;
  master_conn_->set_data<s_conn_type>(s_conn_data::type, s_conn_type::master);
}

auto disconnected_to_master() -> void {
  master_conn_ = nullptr;
}

auto master_conn() -> std::shared_ptr<common::connection> {
  return master_conn_;
}

/************************************************************************************************************** */
/************************************************************************************************************** */

static auto client_conns = std::set<std::shared_ptr<common::connection>>{};

static auto client_conns_mut = std::mutex{};

auto regist_client(std::shared_ptr<common::connection> conn) -> void {
  conn->set_data<s_conn_type>(s_conn_data::type, s_conn_type::client);
  auto lock = std::unique_lock{client_conns_mut};
  client_conns.emplace(conn);
}

auto unregist_client(std::shared_ptr<common::connection> conn) -> void {
  auto lock = std::unique_lock{client_conns_mut};
  client_conns.erase(conn);
}

/************************************************************************************************************** */
/************************************************************************************************************** */

static auto storage_conns = std::set<std::shared_ptr<common::connection>>{};

static auto storage_conns_mut = std::mutex{};

auto regist_storage(std::shared_ptr<common::connection> conn) -> void {
  unregist_client(conn);
  conn->set_data<s_conn_type>(s_conn_data::type, s_conn_type::storage);
  auto lock = std::unique_lock{storage_conns_mut};
  storage_conns.emplace(conn);
}

auto unregist_storage(std::shared_ptr<common::connection> conn) -> void {
  auto lock = std::unique_lock{storage_conns_mut};
  storage_conns.erase(conn);
}

auto registed_storages() -> std::set<std::shared_ptr<common::connection>> {
  auto lock = std::unique_lock{storage_conns_mut};
  return storage_conns;
}

/************************************************************************************************************** */
/************************************************************************************************************** */

auto ss_regist_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool> {
  auto request_data_recved = proto::ss_regist_request{};
  if (!request_data_recved.ParseFromArray(request_recved->data, request_recved->data_len)) {
    LOG_ERROR(std::format("parse ss_regist_request failed"));
    co_await conn->send_response(common::proto_frame{.stat = 1}, request_recved);
    co_return false;
  }

  if (request_data_recved.master_magic() != sss_config.master_magic ||
      request_data_recved.storage_magic() != sss_config.storage_magic) {
    LOG_ERROR(std::format("ss_regist request invalid magic, master: {}, storage: {}",
                          request_data_recved.master_magic(), request_data_recved.storage_magic()));
    co_await conn->send_response(common::proto_frame{.stat = 2}, request_recved);
    co_return false;
  }

  co_await conn->send_response(common::proto_frame{.stat = 0}, request_recved);
  regist_storage(conn);
  LOG_INFO(std::format("storage regist suc {}:{}", conn->ip(), conn->port()));
  co_return true;
}

auto ss_upload_sync_open_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool> {
  if (conn->has_data(s_conn_data::storage_sync_upload_file_id)) {
    LOG_ERROR("storage already request sync upload yield");
    co_await conn->send_response(common::proto_frame{.stat = 1}, request_recved);
    co_return false;
  }

  if (request_recved->data_len <= sizeof(uint64_t)) {
    LOG_ERROR("ss_upload_sync_open request data_len invalid");
    co_await conn->send_response(common::proto_frame{.stat = 2}, request_recved);
    co_return false;
  }

  auto rel_path = std::string_view{request_recved->data + sizeof(uint64_t), request_recved->data_len - sizeof(uint64_t)};
  auto file_id = hot_store_group()->create_file(ntohll(*(uint64_t *)request_recved->data), rel_path);
  if (!file_id) {
    LOG_ERROR(std::format("create file '{}' failed", rel_path));
    co_await conn->send_response(common::proto_frame{.stat = 3}, request_recved);
    co_return false;
  }

  conn->set_data<uint64_t>(s_conn_data::storage_sync_upload_file_id, file_id.value());
  co_await conn->send_response(common::proto_frame{.stat = 0}, request_recved);
  co_return true;
}

auto ss_upload_sync_append_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool> {
  auto file_id = conn->get_data<uint64_t>(s_conn_data::storage_sync_upload_file_id);
  if (!file_id.has_value()) {
    LOG_ERROR("storage not request sync upload yield");
    co_await conn->send_response(common::proto_frame{.stat = 1}, request_recved);
    co_return false;
  }

  /* 结束同步 */
  if (request_recved->data_len == 0) {
    conn->del_data(s_conn_data::storage_sync_upload_file_id);

    auto res = hot_store_group()->close_write_file(file_id.value());
    if (!res) {
      co_await conn->send_response(common::proto_frame{.stat = 2}, request_recved);
      co_return false;
    }
    const auto &[root_path, rel_path] = res.value();

    co_await conn->send_response(common::proto_frame{.stat = 0}, request_recved);
    new_hot_file(std::format("{}/{}", root_path, rel_path));
    co_return true;
  }

  if (!hot_store_group()->write_file(file_id.value(), std::span{request_recved->data, request_recved->data_len})) {
    co_await conn->send_response(common::proto_frame{.stat = 3}, request_recved);
    conn->del_data(s_conn_data::storage_sync_upload_file_id);
    co_return false;
  }

  co_await conn->send_response(common::proto_frame{.stat = 0}, request_recved);
  co_return true;
}

auto ms_get_max_free_space_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool> {
  auto response_to_send = std::shared_ptr<common::proto_frame>{(common::proto_frame *)malloc(sizeof(common::proto_frame) + sizeof(uint64_t)), free};
  *response_to_send = {.data_len = sizeof(uint64_t)};
  *(uint64_t *)response_to_send->data = ntohll(hot_store_group()->max_free_space());
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
  if (conn->has_data(s_conn_data::client_upload_file_id)) {
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
  auto file_id = hot_store_group()->create_file(file_size);
  if (!file_id) {
    LOG_ERROR(std::format("create file failed for file_size {}", file_size));
    co_await conn->send_response(common::proto_frame{.stat = 3}, request_recved);
    co_return false;
  }
  conn->set_data<uint64_t>(s_conn_data::client_upload_file_id, file_id.value());
  co_await conn->send_response(common::proto_frame{.stat = 0}, request_recved);
  co_return true;
}

auto cs_upload_append_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool> {
  auto file_id = conn->get_data<uint64_t>(s_conn_data::client_upload_file_id);
  if (!file_id) {
    LOG_ERROR("client not request upload yield");
    co_await conn->send_response(common::proto_frame{.stat = 1}, request_recved);
    co_return false;
  }

  if (request_recved->data_len == 0) {
    LOG_ERROR(std::format("client cancel upload"));
    co_await conn->send_response(common::proto_frame{.stat = 2}, request_recved);
    conn->del_data(s_conn_data::client_upload_file_id);
    co_return false;
  }

  if (!hot_store_group()->write_file(file_id.value(), std::span{request_recved->data, request_recved->data_len})) {
    co_await conn->send_response(common::proto_frame{.stat = 3}, request_recved);
    conn->del_data(s_conn_data::client_upload_file_id);
    co_return false;
  }

  co_await conn->send_response(common::proto_frame{.stat = 0}, request_recved);
  co_return true;
}

auto cs_upload_close_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool> {
  auto file_id = conn->get_data<uint64_t>(s_conn_data::client_upload_file_id);
  if (!file_id) {
    LOG_ERROR("client not request upload yield");
    co_await conn->send_response(common::proto_frame{.stat = 1}, request_recved);
    co_return false;
  }

  auto res = hot_store_group()->close_write_file(file_id.value(), std::string_view{request_recved->data, request_recved->data_len});
  if (!res) {
    LOG_ERROR("close file failed");
    co_await conn->send_response(common::proto_frame{.stat = 2}, request_recved);
    co_return false;
  }
  const auto &[root_path, rel_path] = res.value();
  push_not_synced_file(rel_path);

  /* 在 rel_path 前加上组号，用于客户端访问文件 */
  auto rel_path_with_group = std::format("{}/{}", sss_config.group_id, rel_path);
  auto response_to_send = std::shared_ptr<common::proto_frame>{(common::proto_frame *)malloc(sizeof(common::proto_frame) + rel_path.size()), free};
  *response_to_send = {.data_len = (uint32_t)rel_path_with_group.size()};
  std::copy(rel_path_with_group.begin(), rel_path_with_group.end(), response_to_send->data);
  co_await conn->send_response(response_to_send.get(), request_recved);

  new_hot_file(std::format("{}/{}", root_path, rel_path));
  conn->del_data(s_conn_data::client_upload_file_id);
  co_return true;
}

auto cs_download_open_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool> {
  if (conn->has_data(s_conn_data::client_download_file_id)) {
    LOG_ERROR("client already request download yield");
    co_await conn->send_response(common::proto_frame{.stat = 1}, request_recved);
    co_return false;
  }

  auto valid_store_group = std::shared_ptr<store_ctx_group>{};
  auto filesize = 0ul;
  for (auto store_group : iterate_store_groups()) {
    if (auto res = store_group->open_read_file({request_recved->data, request_recved->data_len}); res.has_value()) {
      valid_store_group = store_group;
      auto [file_id, file_size, abs_path] = res.value();

      conn->set_data<uint64_t>(s_conn_data::client_download_file_id, file_id);
      conn->set_data<std::shared_ptr<store_ctx_group>>(s_conn_data::client_download_store_group, store_group);

      if (is_hot_store_group(store_group)) {
        access_hot_file(abs_path);
      } else {
        access_cold_file(abs_path);
      }
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
  auto file_id = conn->get_data<uint64_t>(s_conn_data::client_download_file_id);
  if (!file_id) {
    LOG_ERROR("client not request download yield");
    co_await conn->send_response(common::proto_frame{.stat = 1}, request_recved);
    co_return false;
  }

  auto store_group = conn->get_data<std::shared_ptr<store_ctx_group>>(s_conn_data::client_download_store_group);
  if (!store_group) {
    LOG_ERROR("unknownd internal error");
    co_await conn->send_response(common::proto_frame{.stat = 2}, request_recved);
    co_return false;
  }

  auto response_to_send = std::shared_ptr<common::proto_frame>{(common::proto_frame *)malloc(sizeof(common::proto_frame) + 5_MB), free};
  auto read_len = store_group.value()->read_file(file_id.value(), response_to_send->data, 5_MB);
  if (!read_len.has_value()) {
    co_await conn->send_response(common::proto_frame{.stat = 3}, request_recved);
    co_return false;
  }
  *response_to_send = {.data_len = (uint32_t)read_len.value()};
  co_await conn->send_response(response_to_send.get(), request_recved);
  co_return true;
}
