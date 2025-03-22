#include "migrate_service.h"
#include "common/exception_handle.h"
#include "common/log.h"
#include "common/util.h"
#include "storage_config.h"
#include <sys/stat.h>

#define SMS_TO_COLD_DISABLE 0
#define SMS_TO_COLD_ATIME 1
#define SMS_TO_COLD_CTIME 2
#define SMS_TO_HOT_DISABLE 0
#define SMS_TO_HOT_TIMES 1

/******************************************************************************* */
/* 热数据迁移到冷数据 */
/******************************************************************************* */

static auto hot_file_atime_or_ctime = std::map<std::string, uint64_t>{};
static auto hot_file_atime_or_ctime_mut = std::mutex{};

static auto init_migrate_to_cold() -> void {
  if (storage_config.migrate_service.to_cold_rule == SMS_TO_COLD_DISABLE) {
    LOG_INFO("diable to cold migrate");
    return;
  }

  LOG_INFO("to cold migrate init start");
  struct stat st;
  for (const auto &path : storage_config.storage_service.hot_paths) {
    for (const auto &file : common::iterate_normal_file(path)) {
      if (auto ret = stat(file.c_str(), &st); ret != 0) {
        LOG_ERROR(std::format("stat failed '{}' {}", file, ret));
      }

      switch (storage_config.migrate_service.to_cold_rule) {
        case SMS_TO_COLD_ATIME:
          hot_file_atime_or_ctime[file] = st.st_atime;
          break;
        case SMS_TO_COLD_CTIME:
          hot_file_atime_or_ctime[file] = st.st_ctime;
          break;
        default:
          LOG_CRITICAL("unknown to cold rule {}", storage_config.migrate_service.to_cold_rule);
          quit_storage_service();
          break;
      }
    }
  }
  LOG_INFO("to cold migrate init suc");
}

/******************************************************************************* */
/* 冷数据迁移到热数据 */
/******************************************************************************* */

static auto cold_file_access_times = std::map<std::string, uint64_t>{};
static auto cold_file_access_times_mut = std::mutex{};

static auto init_migrate_to_hot() -> void {
  if (storage_config.migrate_service.to_hot_rule == SMS_TO_HOT_DISABLE) {
    LOG_INFO("disable to hot migrate");
    return;
  }

  LOG_INFO("to hot migrate init start");
  for (const auto &path : storage_config.storage_service.cold_paths) {
    for (const auto &file : common::iterate_normal_file(path)) {
      cold_file_access_times[file] = 0;
    }
  }
  LOG_INFO("to hot migrate init suc");
}

/******************************************************************************* */
/******************************************************************************* */

static auto migrate_to_cold_once(std::string_view abs_path) -> asio::awaitable<void> {
  LOG_INFO(std::format("migrate to cold {}", abs_path));
  co_return;
}

static auto migrate_to_cold_service() -> asio::awaitable<void> {
  if (storage_config.migrate_service.to_cold_action == SMS_TO_COLD_DISABLE) {
    co_return;
  }

  auto to_cold_files = std::vector<std::string>{};
  for (const auto &[abs_path, time] : hot_file_atime_or_ctime) {
    if ((int64_t)time + storage_config.migrate_service.to_cold_action < std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count()) {
      to_cold_files.push_back(abs_path);
    }
  }

  for (const auto &file : to_cold_files) {
    co_await migrate_to_cold_once(file);
  }
}

static auto migrate_to_hot_once(std::string_view abs_path) -> asio::awaitable<void> {
  LOG_INFO(std::format("migrate to hot {}", abs_path));
  co_return;
}

static auto migrate_to_hot_service() -> asio::awaitable<void> {
  if (storage_config.migrate_service.to_hot_action == SMS_TO_HOT_DISABLE) {
    co_return;
  }

  auto to_hot_files = std::vector<std::string>{};
  for (const auto &[abs_path, times] : cold_file_access_times) {
    if (times >= storage_config.migrate_service.to_hot_action) {
      to_hot_files.push_back(abs_path);
    }
  }

  for (const auto &file : to_hot_files) {
    co_await migrate_to_hot_once(file);
  }
}

static auto migrate_service_timer = std::unique_ptr<asio::steady_timer>{};

auto migrate_service() -> asio::awaitable<void> {
  migrate_service_timer = std::make_unique<asio::steady_timer>(co_await asio::this_coro::executor);
  while (true) {
    migrate_service_timer->expires_after(std::chrono::seconds{1000});
    co_await migrate_service_timer->async_wait(asio::as_tuple(asio::use_awaitable));

    LOG_INFO("start migrate service");
    co_await migrate_to_cold_service();
    co_await migrate_to_hot_service();
  }
}

auto init_migrate_service() -> asio::awaitable<void> {
  init_migrate_to_cold();
  init_migrate_to_hot();
  asio::co_spawn(co_await asio::this_coro::executor, migrate_service(), common::exception_handle);
  co_return;
}

auto start_migrate_service() -> void {
  if (migrate_service_timer) {
    migrate_service_timer->cancel();
  }
}

auto new_hot_file(const std::string &abs_path) -> void {
  LOG_DEBUG(std::format("new hot file {}", abs_path));
  switch (storage_config.migrate_service.to_cold_rule) {
    case SMS_TO_COLD_DISABLE: {
      break;
    }
    case SMS_TO_COLD_ATIME:
    case SMS_TO_COLD_CTIME: {
      auto lock = std::unique_lock{hot_file_atime_or_ctime_mut};
      hot_file_atime_or_ctime[abs_path] = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
      break;
    }
  }
}

auto access_hot_file(const std::string &abs_path) -> void {
  if (storage_config.migrate_service.to_cold_rule != SMS_TO_COLD_ATIME) {
    return;
  }

  auto lock = std::unique_lock{hot_file_atime_or_ctime_mut};
  auto old_time = hot_file_atime_or_ctime[abs_path];
  auto new_time = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  hot_file_atime_or_ctime[abs_path] = new_time;
  LOG_DEBUG(std::format("access hot file {} atime {} -> {}", abs_path, old_time, new_time));
}

auto access_cold_file(const std::string &abs_path) -> void {
  if (storage_config.migrate_service.to_hot_rule != SMS_TO_HOT_TIMES) {
    return;
  }

  auto lock = std::unique_lock{cold_file_access_times_mut};
  auto old_times = cold_file_access_times[abs_path];
  auto new_times = ++old_times;
  cold_file_access_times[abs_path] = new_times;
  LOG_DEBUG(std::format("access cold file {} times {} -> {}", abs_path, old_times, new_times));
}
