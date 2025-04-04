#include "storage_service_handles.h"
#include "common/log.h"
#include "common/metrics_service.h"
#include "common/util.h"
#include "migrate_service.h"
#include "proto/proto.pb.h"
#include "storage_config.h"
#include <sys/sendfile.h>

static auto hot_store_group_ = std::shared_ptr<store_ctx_group>{};
static auto cold_store_group_ = std::shared_ptr<store_ctx_group>{};

auto init_store_group() -> void {
  hot_store_group_ = std::make_shared<store_ctx_group>("hot_store_group", storage_config.storage_service.hot_paths);
  cold_store_group_ = std::make_shared<store_ctx_group>("cold_store_group", storage_config.storage_service.cold_paths);
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
  master_conn_->set_data<s_conn_type>(s_conn_data::x_type, s_conn_type::master);
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
  conn->set_data<s_conn_type>(s_conn_data::x_type, s_conn_type::client);
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
  conn->set_data<s_conn_type>(s_conn_data::x_type, s_conn_type::storage);
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
    co_await conn->send_response(common::proto_frame{.stat = 1}, *request_recved);
    co_return false;
  }

  if (request_data_recved.master_magic() != storage_config.storage_service.master_magic ||
      request_data_recved.storage_magic() != storage_config.storage_service.internal.storage_magic) {
    LOG_ERROR(std::format("ss_regist request invalid magic, master magic 0x{:X}/0x{:X}, storage_magic 0x{:X}/0x{:X}",
                          request_data_recved.master_magic(), storage_config.storage_service.master_magic,
                          request_data_recved.storage_magic(), storage_config.storage_service.internal.storage_magic));
    co_await conn->send_response(common::proto_frame{.stat = 2}, *request_recved);
    co_return false;
  }

  co_await conn->send_response(common::proto_frame{.stat = 0}, *request_recved);
  regist_storage(conn);
  LOG_INFO(std::format("storage regist suc {}:{}", conn->ip(), conn->port()));
  co_return true;
}

auto ss_upload_sync_start_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool> {
  if (conn->has_data(s_conn_data::s_sync_up_fileid)) {
    LOG_ERROR("storage already request sync upload yield");
    co_await conn->send_response(common::proto_frame{.stat = 1}, *request_recved);
    co_return false;
  }

  if (request_recved->data_len <= sizeof(uint64_t)) {
    LOG_ERROR("ss_upload_sync_start request data_len invalid");
    co_await conn->send_response(common::proto_frame{.stat = 2}, *request_recved);
    co_return false;
  }

  auto rel_path = std::string_view{request_recved->data + sizeof(uint64_t), request_recved->data_len - sizeof(uint64_t)};
  auto file_id = hot_store_group()->create_file(common::ntohll(*(uint64_t *)request_recved->data), rel_path);
  if (!file_id) {
    LOG_ERROR(std::format("create file '{}' failed", rel_path));
    co_await conn->send_response(common::proto_frame{.stat = 3}, *request_recved);
    co_return false;
  }

  conn->set_data<uint64_t>(s_conn_data::s_sync_up_fileid, file_id.value());
  co_await conn->send_response(common::proto_frame{.stat = 0}, *request_recved);
  co_return true;
}

auto ss_upload_sync_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool> {
  auto file_id = conn->get_data<uint64_t>(s_conn_data::s_sync_up_fileid);
  if (!file_id.has_value()) {
    LOG_ERROR("storage not request sync upload yield");
    co_await conn->send_response(common::proto_frame{.stat = 1}, *request_recved);
    co_return false;
  }

  /* 结束同步 */
  if (request_recved->data_len == 0 || request_recved->stat == 255) {
    conn->del_data(s_conn_data::s_sync_up_fileid);

    auto res = hot_store_group()->close_write_file(file_id.value());
    if (!res) {
      co_await conn->send_response(common::proto_frame{.stat = 2}, *request_recved);
      co_return false;
    }
    const auto &[root_path, rel_path] = res.value();

    co_await conn->send_response(common::proto_frame{.stat = 0}, *request_recved);
    new_hot_file(std::format("{}/{}", root_path, rel_path));
    LOG_INFO("sync file {} suc from {}", rel_path, conn->address());
    co_return true;
  }

  if (!hot_store_group()->write_file(file_id.value(), std::span{request_recved->data, request_recved->data_len})) {
    co_await conn->send_response(common::proto_frame{.stat = 3}, *request_recved);
    conn->del_data(s_conn_data::s_sync_up_fileid);
    co_return false;
  }

  co_await conn->send_response(common::proto_frame{.stat = 0}, *request_recved);
  co_return true;
}

auto ms_get_max_free_space_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool> {
  auto response_to_send = std::shared_ptr<common::proto_frame>{(common::proto_frame *)malloc(sizeof(common::proto_frame) + sizeof(uint64_t)), free};
  *response_to_send = {.data_len = sizeof(uint64_t)};
  *(uint64_t *)response_to_send->data = common::ntohll(hot_store_group()->max_free_space());
  co_await conn->send_response(response_to_send.get(), *request_recved);
  co_return true;
}

auto ms_get_metrics_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool> {
  auto s = metrics::get_metrics_as_string();
  auto response_to_send = std::shared_ptr<common::proto_frame>{(common::proto_frame *)malloc(sizeof(common::proto_frame) + s.size()), free};
  *response_to_send = {.data_len = (uint32_t)s.size()};
  std::copy(s.begin(), s.end(), response_to_send->data);
  co_await conn->send_response(response_to_send.get(), *request_recved);
  co_return true;
}

auto cs_upload_start_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool> {
  if (conn->has_data(s_conn_data::c_up_fileid)) {
    LOG_ERROR("client already request upload yield");
    co_await conn->send_response({.stat = 1}, *request_recved);
    co_return false;
  }

  if (request_recved->data_len != sizeof(uint64_t)) {
    LOG_ERROR("cs_upload_start request data_len invalid");
    co_await conn->send_response({.stat = 2}, *request_recved);
    co_return false;
  }

  auto file_size = common::ntohll(*(uint64_t *)request_recved->data);
  auto file_id = hot_store_group()->create_file(file_size);
  if (!file_id) {
    LOG_ERROR(std::format("create file failed for file_size {}", file_size));
    co_await conn->send_response({.stat = 3}, *request_recved);
    co_return false;
  }
  conn->set_data<uint64_t>(s_conn_data::c_up_fileid, file_id.value());
  co_await conn->send_response(*request_recved);
  co_return true;
}

auto cs_upload_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool> {
  auto file_id = conn->get_data<uint64_t>(s_conn_data::c_up_fileid);
  if (!file_id) {
    LOG_ERROR("client not start upload yield");
    co_await conn->send_response({.stat = 1}, *request_recved);
    co_return false;
  }

  /* 上传完成 */
  if (request_recved->stat == common::FRAME_STAT_FINISH) {
    auto res = hot_store_group()->close_write_file(file_id.value(), std::string_view{request_recved->data, request_recved->data_len});
    if (!res) {
      LOG_ERROR("close file failed");
      co_await conn->send_response({.stat = 2}, *request_recved);
      co_return false;
    }
    const auto &[root_path, rel_path] = res.value();
    push_not_synced_file(rel_path);

    /* 在 rel_path 前加上组号，用于客户端访问文件 */
    auto rel_path_with_group = std::format("{}/{}", storage_config.storage_service.internal.group_id, rel_path);
    auto response_to_send = common::create_frame(request_recved->cmd, common::frame_type::response, rel_path_with_group.size());
    std::copy(rel_path_with_group.begin(), rel_path_with_group.end(), response_to_send->data);
    co_await conn->send_response(response_to_send.get(), *request_recved);

    new_hot_file(std::format("{}/{}", root_path, rel_path));
    conn->del_data(s_conn_data::c_up_fileid);
    co_return true;
  }

  /* 上传异常 */
  if (request_recved->stat != common::FRAME_STAT_OK) {
    LOG_ERROR("client upload unknown error {}", request_recved->stat);
    hot_store_group()->close_write_file(file_id.value());
    conn->del_data(s_conn_data::c_up_fileid);
    co_await conn->send_response(*request_recved);
    co_return false;
  }

  /* 正常传输的数据 */
  if (!hot_store_group()->write_file(file_id.value(), std::span{request_recved->data, request_recved->data_len})) {
    co_await conn->send_response({.stat = 3}, *request_recved);
    conn->del_data(s_conn_data::c_up_fileid);
    co_return false;
  }

  co_await conn->send_response(*request_recved);
  co_return true;
}

auto cs_download_start_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool> {
  if (conn->has_data(s_conn_data::c_down_file_id)) {
    LOG_ERROR("client already request download yield");
    co_await conn->send_response(common::proto_frame{.stat = 1}, *request_recved);
    co_return false;
  }

  /* 搜索文件 */
  auto valid_store_group = std::shared_ptr<store_ctx_group>{};
  auto file_id = 0uz;
  auto file_size = 0uz;
  auto abs_path = std::string{};
  for (auto store_group : iterate_store_groups()) {
    if (auto res = store_group->open_read_file({request_recved->data, request_recved->data_len}); res.has_value()) {
      valid_store_group = store_group;
      std::tie(file_id, file_size, abs_path) = res.value();

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
    co_await conn->send_response(common::proto_frame{.stat = 2}, *request_recved);
    co_return false;
  }

  if (file_size <= storage_config.performance.zero_copy_limit * 1_MB) {
    conn->set_data<std::string>(s_conn_data::c_down_file_path, abs_path);
    conn->set_data<uint32_t>(s_conn_data::c_down_file_size, file_size);
  } else {
    conn->set_data<uint64_t>(s_conn_data::c_down_file_id, file_id);
    conn->set_data<std::shared_ptr<store_ctx_group>>(s_conn_data::c_down_store_group, valid_store_group);
  }

  auto response_to_send = common::create_frame(request_recved->cmd, common::frame_type::response, sizeof(uint64_t));
  *(uint64_t *)response_to_send->data = common::htonll(file_size);
  co_return co_await conn->send_response(response_to_send.get(), *request_recved);
}

auto cs_download_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool> {
  /* 普通下载 */
  if (auto file_id = conn->get_data<uint64_t>(s_conn_data::c_down_file_id)) {
    auto store_group = conn->get_data<std::shared_ptr<store_ctx_group>>(s_conn_data::c_down_store_group).value();
    auto response_to_send = common::create_frame(request_recved->cmd, common::frame_type::response, 5_MB);
    auto read_len = store_group->read_file(file_id.value(), response_to_send->data, 5_MB);
    if (!read_len.has_value()) {
      co_await conn->send_response({.stat = 1}, *request_recved);
      co_return false;
    }

    if (read_len.value() != response_to_send->data_len) {
      response_to_send->data_len = (uint32_t)read_len.value();
      response_to_send->stat = common::FRAME_STAT_FINISH;
    }
    co_return co_await conn->send_response(response_to_send.get(), *request_recved);
  }

  /* 零拷贝优化 */
  if (auto abs_path = conn->get_data<std::string>(s_conn_data::c_down_file_path)) {
    auto file_fd = open(abs_path->data(), O_RDONLY);
    if (file_fd < 0) {
      LOG_ERROR("open file {} failed, {}", abs_path.value(), strerror(errno));
      co_return false;
    }

    auto file_size = conn->get_data<uint32_t>(s_conn_data::c_down_file_size).value();
    if (!co_await conn->send_response_without_data({.stat = common::FRAME_STAT_FINISH, .data_len = (uint32_t)file_size}, *request_recved)) {
      co_return false;
    }

    auto rest_to_send = file_size;
    auto offset = off_t{0};
    while (rest_to_send > 0) {
      auto n = sendfile(conn->native_socket(), file_fd, &offset, rest_to_send);
      if (-1 == n) {
        if (errno == EAGAIN || errno == EINTR) {
          co_await asio::post(asio::use_awaitable);
          continue;
        }

        LOG_CRITICAL("sendfile {} failed, {}", abs_path.value(), strerror(errno));
        co_await conn->close();
        break;
      }

      rest_to_send -= n;
      LOG_DEBUG("sendfile {} bytes, rest to send {} bytes", n, rest_to_send);
    }
    co_return true;
  }

  LOG_ERROR("client not start download yield");
  co_await conn->send_response({.stat = 2}, *request_recved);
  co_return false;
}
