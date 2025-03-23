#include "sync_service.h"
#include "common/exception_handle.h"
#include "common/log.h"
#include "storage_config.h"
#include "storage_service_handles.h"
#include <queue>
#include <sys/sendfile.h>
import common;

/* 双缓冲队列 */
static auto not_sync_files = std::queue<std::string>{};
static auto not_sync_files_bk = std::queue<std::string>{};

static auto not_synced_files_mut = std::mutex{};

static auto sync_service_timer = std::unique_ptr<asio::steady_timer>{};

auto sync_service() -> asio::awaitable<void> {
  sync_service_timer = std::make_unique<asio::steady_timer>(co_await asio::this_coro::executor);
  while (true) {
    sync_service_timer->expires_after(std::chrono::seconds{100});
    co_await sync_service_timer->async_wait(asio::as_tuple(asio::use_awaitable));

    if (registed_storages().size() <= 0) {
      LOG_WARN("no storage to sync file");
      continue;
    }

    auto btime = std::chrono::steady_clock::now();
    auto total_file = 0;
    for (const auto &rel_path : pop_not_synced_files()) {
      auto res = hot_store_group()->open_read_file(rel_path);
      if (!res) {
        LOG_WARN("open not synced file {} failed", rel_path);
        continue;
      }
      auto [file_id, file_size, abs_path] = res.value();

      if (file_size <= storage_config.performance.zero_copy_limit * 1_MB) {
        co_await sync_file_zero_copy(rel_path, file_id, file_size, abs_path);
      } else {
        co_await sync_file_normal(rel_path, file_id, file_size, abs_path);
      }

      ++total_file;
      hot_store_group()->close_read_file(file_id);
    }
    auto etime = std::chrono::steady_clock::now();
    LOG_INFO("sync service suc, sync {} files cost {}ms", total_file, std::chrono::duration_cast<std::chrono::milliseconds>(etime - btime).count());
  }
}

auto init_sync_service() -> asio::awaitable<void> {
  asio::co_spawn(co_await asio::this_coro::executor, sync_service(), common::exception_handle);
  co_return;
}

auto start_sync_service() -> void {
  if (sync_service_timer) {
    sync_service_timer->cancel();
  }
}

auto get_valid_syncable_storages(std::string_view rel_path, uint64_t file_id, uint64_t file_size, std::string_view abs_path) -> asio::awaitable<std::vector<std::shared_ptr<common::connection>>> {
  auto res = std::vector<std::shared_ptr<common::connection>>{};

  auto request_to_send = common::create_frame(common::proto_cmd::ss_upload_sync_start, common::frame_type::request, sizeof(uint64_t) + rel_path.size());
  *(uint64_t *)request_to_send->data = common::htonll(file_size);
  std::copy(rel_path.begin(), rel_path.end(), request_to_send->data + sizeof(uint64_t));

  for (auto s_conn : registed_storages()) {
    auto response_recved = co_await s_conn->send_request_and_wait_response(request_to_send.get());
    if (!response_recved || response_recved->stat != 0) {
      LOG_ERROR("storage {} is unable to sync file {} {}", s_conn->address(), abs_path, response_recved ? response_recved->stat : -1);
      continue;
    }
    res.push_back(s_conn);
  }

  if (res.empty()) {
    push_not_synced_file(rel_path);
    LOG_ERROR("no valid storage to sync file {}", abs_path);
  }
  co_return res;
}

auto sync_file_normal(std::string_view rel_path, uint64_t file_id, uint64_t file_size, std::string_view abs_path) -> asio::awaitable<bool> {
  auto valid_storages = co_await get_valid_syncable_storages(rel_path, file_id, file_size, abs_path);
  if (valid_storages.empty()) {
    co_return false;
  }

  LOG_INFO("sync {} in normal way", abs_path);

  auto request_to_send = common::create_frame(common::proto_cmd::ss_upload_sync, common::frame_type::request, 5_MB);
  while (true) {
    auto read_len = hot_store_group()->read_file(file_id, request_to_send->data, 5 * 1024 * 1024);
    if (!read_len.has_value()) {
      break;
    }
    request_to_send->data_len = read_len.value();

    for (auto storage : valid_storages) {
      auto response_recved = co_await storage->send_request_and_wait_response(request_to_send.get());
      if (!response_recved || response_recved->stat != 0) {
        LOG_ERROR("sync file {} append failed, {}", abs_path, response_recved ? response_recved->stat : -1);
        continue;
      }
    }

    if (read_len == 0) {
      break;
    }
  }
  co_return true;
}

auto sync_file_zero_copy(std::string_view rel_path, uint64_t file_id, uint64_t file_size, std::string_view abs_path) -> asio::awaitable<bool> {
  auto valid_storages = co_await get_valid_syncable_storages(rel_path, file_id, file_size, abs_path);
  if (valid_storages.empty()) {
    co_return false;
  }

  LOG_INFO("sync {} with zero copy", abs_path);

  /* 零拷贝优化 */
  auto file_fd = open(abs_path.data(), O_RDONLY);
  if (file_fd < 0) {
    LOG_ERROR("open file {} failed, {}", abs_path, strerror(errno));
    co_return false;
  }

  for (auto storage : valid_storages) {
    auto id = co_await storage->send_request_without_data(common::proto_frame{.cmd = common::proto_cmd::ss_upload_sync, .stat = 255, .data_len = (uint32_t)file_size});

    auto rest_to_send = file_size;
    auto offset = off_t{0};
    while (rest_to_send > 0) {
      auto n = sendfile(storage->native_socket(), file_fd, &offset, rest_to_send);
      if (-1 == n) {
        if (errno == EAGAIN || errno == EINTR) {
          co_await asio::post(asio::use_awaitable);
          continue;
        }

        LOG_CRITICAL("send file {} failed, {}", abs_path, strerror(errno));
        break;
      }

      rest_to_send -= n;
      LOG_DEBUG("send {} bytes, rest to send {} bytes", n, rest_to_send);
    }

    if (rest_to_send != 0) {
      continue;
    }

    auto response_recved = co_await storage->recv_response(id.value());
    if (!response_recved || response_recved->stat != 0) {
      LOG_ERROR("sync file {} append failed, {}", abs_path, response_recved ? response_recved->stat : -1);
      continue;
    }
  }

  co_return true;
}

auto push_not_synced_file(std::string_view rel_path) -> void {
  auto lock = std::unique_lock{not_synced_files_mut};
  not_sync_files.emplace(rel_path);
}

auto pop_not_synced_files() -> std::generator<std::string> {
  {
    auto lock = std::unique_lock{not_synced_files_mut};
    not_sync_files_bk = std::move(not_sync_files);
  }

  while (!not_sync_files_bk.empty()) {
    auto ret = not_sync_files_bk.front();
    not_sync_files_bk.pop();
    co_yield ret;
  }
}

auto not_synced_file_count() -> size_t {
  auto lock = std::unique_lock{not_synced_files_mut};
  return not_sync_files.size();
}