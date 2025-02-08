#include "./storage_service_global.h"

auto sync_upload_files() -> asio::awaitable<void> {
  while (true) {
    co_await co_sleep_for(std::chrono::seconds{conf.sync_interval});

    g_log->log_debug(std::format("start sync files unsynced size {}", unsync_uploaded_files.size()));
    if (storage_conns.empty()) {
      g_log->log_debug("no other storages");
      continue;
    }

    while (!unsync_uploaded_files.empty()) {
      /* 获取 relpath */
      auto file_path = std::string{};
      {
        auto lock = std::unique_lock{unsync_uploaded_files_mut};
        file_path = unsync_uploaded_files.front();
        unsync_uploaded_files.pop();
      }

      /* sync open */
      auto res = hot_stores->open_file(file_path);
      if (!res) {
        g_log->log_warn(std::format("open file {} failed", file_path));
        continue;
      }
      auto [file_id, file_size] = res.value();

      auto req_frame = (proto_frame_t *)malloc(sizeof(proto_frame_t) + sizeof(uint64_t) + file_path.size());
      *req_frame = {
          .cmd = (uint32_t)proto_cmd_e::ss_sync_upload_create,
          .data_len = (uint32_t)sizeof(uint64_t) + (uint32_t)file_path.size(),
      };
      *(uint64_t *)(req_frame->data) = file_size;
      std::copy(file_path.begin(), file_path.end(), req_frame->data + sizeof(uint64_t));

      auto need_sync_storages = std::set<std::shared_ptr<connection_t>>{};
      for (auto conn : storage_conns) {
        auto id = co_await conn->send_req_frame(std::shared_ptr<proto_frame_t>{req_frame, [](auto p) { free(p); }});
        if (!id.has_value()) {
          continue;
        }

        auto res_frame = co_await conn->recv_res_frame(id.value());
        if (!res_frame || res_frame->stat != 0) {
          continue;
        }
        need_sync_storages.insert(conn);
      }

      /* sync append */
      req_frame = (proto_frame_t *)malloc(sizeof(proto_frame_t) + 5 * 1024 * 1024);
      while (true) {
        auto data = hot_stores->read_file(file_id, 5 * 1024 * 1024);
        if (data->empty()) {
          break;
        }

        *req_frame = {
            .cmd = (uint32_t)proto_cmd_e::ss_sync_upload_append,
            .data_len = (uint32_t)data->size(),
        };
        std::copy(data->begin(), data->end(), req_frame->data);

        /* 失败则不再同步剩余数据 */
        for (auto conn = need_sync_storages.begin(); conn != need_sync_storages.end();) {
          auto id = co_await (*conn)->send_req_frame(req_frame);
          if (!id.has_value()) {
            conn = need_sync_storages.erase(conn);
            continue;
          }

          auto res_frame = co_await (*conn)->recv_res_frame(id.value());
          if (!res_frame || res_frame->stat != 0) {
            conn = need_sync_storages.erase(conn);
            continue;
          }

          ++conn;
        }
      }
      free(req_frame);

      /* sync finish */
      for (auto conn : need_sync_storages) {
        auto id = co_await conn->send_req_frame(proto_frame_t{
            .cmd = (uint32_t)proto_cmd_e::ss_sync_upload_append,
        });
        if (id.has_value()) {
          co_await conn->recv_res_frame(id.value());
        }
      }
    }
  }
}

auto on_storage_disconnect(std::shared_ptr<connection_t> conn) -> asio::awaitable<void> {
  g_log->log_warn(std::format("storage {} disconnect", conn->to_string()));
  storage_conns.erase(conn);
  co_return;
}

std::map<proto_cmd_e, req_handle_t> storage_req_handles{
    {proto_cmd_e::ss_sync_upload_create, ss_sync_upload_open_handle},
    {proto_cmd_e::ss_sync_upload_append, ss_sync_upload_append_handle},
};

auto ss_sync_upload_open_handle(REQ_HANDLE_PARAMS) -> asio::awaitable<void> {
  if (req_frame->data_len <= sizeof(uint64_t) || conn->has_data((uint64_t)conn_data_e::s_sync_upload_file_id)) {
    co_await conn->send_res_frame(proto_frame_t{.stat = 1}, req_frame);
    co_return;
  }

  /* 解析参数 */
  auto file_size = *(uint64_t *)req_frame->data;
  auto rel_path = std::string_view{req_frame->data + sizeof(uint64_t), req_frame->data_len - sizeof(uint64_t)};
  g_log->log_debug(std::format("async file {} {}", rel_path, file_size));

  /* 创建文件 */
  auto res = hot_stores->create_file(file_size, rel_path);
  if (!res.has_value()) {
    co_await conn->send_res_frame(proto_frame_t{.stat = 2}, req_frame);
    co_return;
  }
  conn->set_data((uint64_t)conn_data_e::s_sync_upload_file_id, res.value());

  /* response */
  co_await conn->send_res_frame(proto_frame_t{.stat = 0}, req_frame);
}

auto ss_sync_upload_append_handle(REQ_HANDLE_PARAMS) -> asio::awaitable<void> {
  auto file_id = conn->get_data<uint64_t>((uint64_t)conn_data_e::s_sync_upload_file_id);
  if (!file_id.has_value()) {
    co_await conn->send_res_frame(proto_frame_t{.stat = 1}, req_frame);
    co_return;
  }

  /* sync finish */
  if (req_frame->data_len == 0) {
    hot_stores->close_file(file_id.value());
    conn->del_data((uint64_t)conn_data_e::s_sync_upload_file_id);
    co_await conn->send_res_frame(proto_frame_t{.stat = 0}, req_frame);
    co_return;
  }

  /* normal append data */
  if (!hot_stores->write_file(file_id.value(), {(uint8_t *)req_frame->data, req_frame->data_len})) {
    co_await conn->send_res_frame(proto_frame_t{.stat = 2}, req_frame);
  } else {
    co_await conn->send_res_frame(proto_frame_t{.stat = 0}, req_frame);
  }
}

auto recv_from_storage(std::shared_ptr<connection_t> conn) -> asio::awaitable<void> {
  while (true) {
    auto req_frame = co_await conn->recv_req_frame();
    if (!req_frame) {
      co_return co_await on_storage_disconnect(conn);
    }

    auto handle = storage_req_handles.find((proto_cmd_e)req_frame->cmd);
    if (handle == storage_req_handles.end()) {
      g_log->log_warn(std::format("unhandle cmd {} from storage {}", req_frame->cmd, conn->to_string()));
      co_await conn->send_res_frame(proto_frame_t{.stat = UINT8_MAX}, req_frame);
      continue;
    }
    co_await handle->second(conn, req_frame);
  }

  co_return;
}
