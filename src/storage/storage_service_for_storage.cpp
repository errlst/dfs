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

      /* open */
      auto res = hot_stores->open_file(file_path);
      if (!res) {
        g_log->log_warn(std::format("open file {} failed", file_path));
        continue;
      }
      auto [file_id, file_size] = res.value();

      /* request */
      auto req_frame = (proto_frame_t *)malloc(sizeof(proto_frame_t) + sizeof(uint64_t) + file_path.size());
      *req_frame = {
          .cmd = (uint32_t)proto_cmd_e::ss_sync_upload_open,
          .data_len = (uint32_t)sizeof(uint64_t) + (uint32_t)file_path.size(),
      };
      *(uint64_t *)(req_frame->data) = file_size;
      std::copy(file_path.begin(), file_path.end(), req_frame->data + sizeof(uint64_t));

      for (auto conn : storage_conns) {
        auto id = co_await conn->send_req_frame(std::shared_ptr<proto_frame_t>{req_frame, [](auto p) { free(p); }});
        if (!id.has_value()) {
          continue;
        }

        /* resposne */
        auto res_frame = co_await conn->recv_res_frame(id.value());
        if (!res_frame) {
          continue;
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

std::map<proto_cmd_e, req_handle_t> storage_req_handles{};

auto recv_from_storage(std::shared_ptr<connection_t> conn) -> asio::awaitable<void> {
  while (true) {
    auto req_frame = co_await conn->recv_req_frame();
    if (!req_frame) {
      co_return co_await on_storage_disconnect(conn);
    }

    auto handle = storage_req_handles.find((proto_cmd_e)req_frame->cmd);
    if (handle == storage_req_handles.end()) {
      g_log->log_warn(std::format("unhandle cmd {} from storage {}", req_frame->cmd, conn->to_string()));
      co_await conn->send_res_frame(proto_frame_t{
                                        .cmd = req_frame->cmd,
                                        .stat = UINT8_MAX,
                                    },
                                    req_frame->id);
      continue;
    }
    co_await handle->second(conn, req_frame);
  }

  co_return;
}
