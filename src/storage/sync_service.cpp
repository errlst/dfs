#include "sync_service.h"
#include "common/util.h"
#include "storage_service_handles.h"
#include <queue>

static auto not_synced_files = std::queue<std::string>{};

static auto not_synced_files_mut = std::mutex{};

static auto sync_service_timer = std::unique_ptr<asio::steady_timer>{};

auto sync_service() -> asio::awaitable<void> {
  sync_service_timer = std::make_unique<asio::steady_timer>(co_await asio::this_coro::executor);
  while (true) {
    sync_service_timer->expires_after(std::chrono::seconds{100});
    co_await sync_service_timer->async_wait(asio::as_tuple(asio::use_awaitable));

    auto btime = std::chrono::steady_clock::now();
    auto total_file = not_synced_file_count();
    auto left_files = total_file;
    for (const auto &rel_path : pop_not_synced_file()) {
      auto res = hot_store_group()->open_read_file(rel_path);
      if (!res) {
        LOG_WARN("open not synced file {} failed", rel_path);
        continue;
      }
      auto [file_id, file_size, abs_path] = res.value();

      /* 请求上传 */
      auto valid_s_conns = std::set<std::shared_ptr<common::connection>>{};
      auto request_to_send = common::create_request_frame(common::proto_cmd::ss_upload_sync_open, sizeof(uint64_t) + rel_path.size());
      *(uint64_t *)request_to_send->data = common::htonll(file_size);
      std::copy(rel_path.begin(), rel_path.end(), request_to_send->data + sizeof(uint64_t));
      for (auto s_conn : registed_storages()) {
        auto response_recved = co_await s_conn->send_request_and_wait_response(request_to_send.get());
        if (!response_recved || response_recved->stat != 0) {
          LOG_ERROR("request to sync file {} failed, {}", abs_path, response_recved ? response_recved->stat : -1);
          continue;
        }
        valid_s_conns.emplace(s_conn);
      }

      if (valid_s_conns.empty()) {
        LOG_ERROR("no valid storage to sync");
        push_not_synced_file(rel_path);
        break;
      }

      /* 上传数据 */
      request_to_send = create_request_frame(common::proto_cmd::ss_upload_sync_append, 5_MB);
      while (true) {
        auto read_len = hot_store_group()->read_file(file_id, request_to_send->data, 5 * 1024 * 1024);
        if (!read_len.has_value()) {
          break;
        }
        request_to_send->data_len = read_len.value();

        for (auto s_conn : valid_s_conns) {
          auto response_recved = co_await s_conn->send_request_and_wait_response(request_to_send.get());
          if (!response_recved || response_recved->stat != 0) {
            LOG_ERROR("sync file {} append failed, {}", abs_path, response_recved ? response_recved->stat : -1);
            continue;
          }
        }

        /* 上传完成 */
        if (read_len == 0) {
          LOG_INFO("sync file {} suc, left {}", abs_path, --left_files);
          break;
        }
      }
    }
    auto etime = std::chrono::steady_clock::now();
    LOG_INFO("sync service suc, sync {} files cost {}ms", total_file, std::chrono::duration_cast<std::chrono::milliseconds>(etime - btime).count());
  }
}

auto start_sync_service() -> void {
  if (sync_service_timer) {
    sync_service_timer->cancel();
  }
}

auto push_not_synced_file(std::string_view rel_path) -> void {
  auto lock = std::unique_lock{not_synced_files_mut};
  not_synced_files.emplace(rel_path);
}

auto pop_not_synced_file() -> std::generator<std::string> {
  auto lock = std::unique_lock{not_synced_files_mut};
  while (!not_synced_files.empty()) {
    auto ret = not_synced_files.front();
    not_synced_files.pop();
    co_yield ret;
  }
}

auto not_synced_file_count() -> size_t {
  auto lock = std::unique_lock{not_synced_files_mut};
  return not_synced_files.size();
}