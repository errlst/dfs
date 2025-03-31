#include "server_for_client.h"
#include "config.h"
#include "migrate.h"
#include "server_util.h"
#include "store_util.h"
#include "sync.h"
#include <common/util.h>
#include <sys/sendfile.h>

namespace storage_detail {

  auto cs_upload_start_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool> {
    if (conn->has_data(conn_data::client_upload_file_id)) {
      LOG_ERROR("client already request upload yield");
      co_await conn->send_response({.stat = 1}, *request);
      co_return false;
    }

    if (request->data_len != sizeof(uint64_t)) {
      LOG_ERROR("cs_upload_start request data_len invalid");
      co_await conn->send_response({.stat = 2}, *request);
      co_return false;
    }

    auto file_size = common::ntohll(*(uint64_t *)request->data);
    auto file_id = hot_store_group()->create_file(file_size);
    if (!file_id) {
      LOG_ERROR(std::format("create file failed for file_size {}", file_size));
      co_await conn->send_response({.stat = 3}, *request);
      co_return false;
    }
    conn->set_data<client_upload_file_id_t>(conn_data::client_upload_file_id, file_id.value());
    co_await conn->send_response(*request);
    co_return true;
  }

  auto cs_upload_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool> {
    auto file_id = conn->get_data<client_upload_file_id_t>(conn_data::client_upload_file_id);
    if (!file_id) {
      LOG_ERROR("client not start upload yield");
      co_await conn->send_response({.stat = 1}, *request);
      co_return false;
    }

    /* 上传完成 */
    if (request->stat == common::FRAME_STAT_FINISH) {
      auto res = hot_store_group()->close_write_file(file_id.value(), std::string_view{request->data, request->data_len});
      if (!res) {
        LOG_ERROR("close file failed");
        co_await conn->send_response({.stat = 2}, *request);
        co_return false;
      }
      const auto &[root_path, rel_path] = res.value();
      push_not_synced_file(rel_path);

      /* 在 rel_path 前加上组号，用于客户端访问文件 */
      auto rel_path_with_group = std::format("{}/{}", storage_config.server.internal.group_id, rel_path);
      auto response_to_send = common::create_frame(request->cmd, common::frame_type::response, rel_path_with_group.size());
      std::copy(rel_path_with_group.begin(), rel_path_with_group.end(), response_to_send->data);
      co_await conn->send_response(response_to_send, *request);

      new_hot_file(std::format("{}/{}", root_path, rel_path));
      conn->del_data(conn_data::client_upload_file_id);
      co_return true;
    }

    /* 上传异常 */
    if (request->stat != common::FRAME_STAT_OK) {
      LOG_ERROR("client upload unknown error {}", request->stat);
      hot_store_group()->close_write_file(file_id.value());
      conn->del_data(conn_data::client_upload_file_id);
      co_await conn->send_response(*request);
      co_return false;
    }

    /* 正常传输的数据 */
    if (!hot_store_group()->write_file(file_id.value(), std::span{request->data, request->data_len})) {
      co_await conn->send_response({.stat = 3}, *request);
      conn->del_data(conn_data::client_upload_file_id);
      co_return false;
    }

    co_await conn->send_response(*request);
    co_return true;
  }

  auto cs_download_start_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool> {
    if (conn->has_data(conn_data::client_download_file_id) ||
        conn->has_data(conn_data::client_download_file_size)) {
      LOG_ERROR("client already request download yield");
      co_await conn->send_response(common::proto_frame{.stat = 1}, *request);
      co_return false;
    }

    /* 搜索文件 */
    auto valid_store_group = std::shared_ptr<store_ctx_group>{};
    auto file_id = 0uz;
    auto file_size = 0uz;
    auto abs_path = std::string{};
    for (auto store_group : store_groups()) {
      if (auto res = store_group->open_read_file({request->data, request->data_len}); res.has_value()) {
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
      LOG_ERROR(std::format("not find file {}", std::string_view{request->data, request->data_len}));
      co_await conn->send_response(common::proto_frame{.stat = 2}, *request);
      co_return false;
    }

    if (file_size <= storage_config.performance.zero_copy_limit * 1_MB) {
      conn->set_data<client_download_file_path_t>(conn_data::client_download_file_path, abs_path);
      conn->set_data<client_download_file_size_t>(conn_data::client_download_file_size, file_size);
    } else {
      conn->set_data<client_download_file_id_t>(conn_data::client_download_file_id, file_id);
      conn->set_data<client_download_store_group_t>(conn_data::client_download_store_group, valid_store_group);
    }

    auto response_to_send = common::create_frame(request->cmd, common::frame_type::response, sizeof(uint64_t));
    *(uint64_t *)response_to_send->data = common::htonll(file_size);
    co_return co_await conn->send_response(response_to_send, *request);
  }

  auto cs_download_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool> {
    /* 普通下载 */
    if (auto file_id = conn->get_data<client_download_file_id_t>(conn_data::client_download_file_id)) {
      auto store_group = conn->get_data<client_download_store_group_t>(conn_data::client_download_store_group).value();
      auto response_to_send = common::create_frame(request->cmd, common::frame_type::response, 5_MB);
      auto read_len = store_group->read_file(file_id.value(), response_to_send->data, 5_MB);
      if (!read_len.has_value()) {
        co_await conn->send_response({.stat = 1}, *request);
        co_return false;
      }

      if (read_len.value() != response_to_send->data_len) {
        response_to_send->data_len = (uint32_t)read_len.value();
        response_to_send->stat = common::FRAME_STAT_FINISH;
      }
      co_return co_await conn->send_response(response_to_send, *request);
    }

    /* 零拷贝优化 */
    if (auto abs_path = conn->get_data<client_download_file_path_t>(conn_data::client_download_file_path)) {
      auto file_fd = open(abs_path->data(), O_RDONLY);
      if (file_fd < 0) {
        LOG_ERROR("open file {} failed, {}", abs_path.value(), strerror(errno));
        co_return false;
      }

      auto file_size = conn->get_data<client_download_file_size_t>(conn_data::client_download_file_size).value();
      if (!co_await conn->send_response_without_data({.stat = common::FRAME_STAT_FINISH, .data_len = (uint32_t)file_size}, *request)) {
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
    co_await conn->send_response({.stat = 2}, *request);
    co_return false;
  }

} // namespace storage_detail

namespace storage {

  using namespace storage_detail;

  auto regist_client(std::shared_ptr<common::connection> conn) -> void {
    conn->set_data<conn_type_t>(conn_data::conn_type, conn_type_t::client);
    auto lock = std::unique_lock{client_conns_mut};
    client_conns.emplace(conn);
  }

  auto unregist_client(std::shared_ptr<common::connection> conn) -> void {
    auto lock = std::unique_lock{client_conns_mut};
    client_conns.erase(conn);
  }

  auto on_client_disconnect(common::connection_ptr conn) -> asio::awaitable<void> {
    unregist_client(conn);
    LOG_INFO("client {} disconnect", conn->address());
    co_return;
  }

  auto request_from_client(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool> {
    auto it = client_request_handles.find(request->cmd);
    if (it != client_request_handles.end()) {
      co_return co_await it->second(request, conn);
    }
    LOG_ERROR("unknown request {} from client {}", *request, conn->address());
    co_return false;
  }

} // namespace storage