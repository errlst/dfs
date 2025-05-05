#include "migrate.h"
#include "config.h"
#include "store_util.h"
#include <common/exception.h>
#include <common/log.h>
#include <common/util.h>

#define SMS_TO_COLD_DISABLE 0
#define SMS_TO_COLD_ATIME 1
#define SMS_TO_COLD_CTIME 2
#define SMS_TO_HOT_DISABLE 0
#define SMS_TO_HOT_TIMES 1

namespace storage_detail
{
  using namespace storage;

  auto init_migrate_to_cold() -> void
  {
    if (storage_config.migrate.to_cold_rule == SMS_TO_COLD_DISABLE)
    {
      LOG_INFO("diable to cold migrate");
      return;
    }

    LOG_INFO("to cold migrate init start");
    struct stat st;
    for (const auto &path : storage_config.server.hot_paths)
    {
      for (const auto &file : common::iterate_normal_file(path))
      {
        if (auto ret = stat(file.c_str(), &st); ret != 0)
        {
          LOG_ERROR(std::format("stat failed '{}' {}", file, ret));
        }

        switch (storage_config.migrate.to_cold_rule)
        {
          case SMS_TO_COLD_ATIME:
            hot_file_atime_or_ctime[file] = st.st_atime;
            break;
          case SMS_TO_COLD_CTIME:
            hot_file_atime_or_ctime[file] = st.st_ctime;
            break;
          default:
            LOG_CRITICAL("unknown to cold rule {}", storage_config.migrate.to_cold_rule);
            break;
        }
      }
    }
    LOG_INFO("to cold migrate init suc");
  }

  auto migrate_to_cold_once(const std::string &abs_path) -> asio::awaitable<void>
  {
    LOG_INFO(std::format("migrate to cold {}", abs_path));
    cold_store_group()->copy_from_another_store(abs_path);
    std::filesystem::remove(abs_path);
    after_hot_to_cold(abs_path);
    co_return;
  }

  auto migrate_to_cold_service() -> asio::awaitable<void>
  {
    if (storage_config.migrate.to_cold_action == SMS_TO_COLD_DISABLE)
    {
      co_return;
    }

    auto to_cold_files = std::vector<std::string>{};
    for (const auto &[abs_path, time] : hot_file_atime_or_ctime)
    {
      if ((int64_t)time + storage_config.migrate.to_cold_timeout < std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count())
      {
        to_cold_files.push_back(abs_path);
      }
    }

    for (const auto &file : to_cold_files)
    {
      co_await migrate_to_cold_once(file);
    }
  }

  auto init_migrate_to_hot() -> void
  {
    if (storage_config.migrate.to_hot_rule == SMS_TO_HOT_DISABLE)
    {
      LOG_INFO("disable to hot migrate");
      return;
    }

    LOG_INFO("to hot migrate init start");
    for (const auto &path : storage_config.server.cold_paths)
    {
      for (const auto &file : common::iterate_normal_file(path))
      {
        cold_file_access_times[file] = 0;
      }
    }
    LOG_INFO("to hot migrate init suc");
  }

  auto migrate_to_hot_once(const std::string &abs_path) -> asio::awaitable<void>
  {
    LOG_INFO(std::format("migrate to hot {}", abs_path));
    hot_store_group()->copy_from_another_store(abs_path);
    std::filesystem::remove(abs_path);
    after_cold_to_hot(abs_path);
    co_return;
  }

  auto migrate_to_hot_service() -> asio::awaitable<void>
  {
    if (storage_config.migrate.to_hot_action == SMS_TO_HOT_DISABLE)
    {
      co_return;
    }

    auto to_hot_files = std::vector<std::string>{};
    for (const auto &[abs_path, times] : cold_file_access_times)
    {
      if (times >= storage_config.migrate.to_hot_action)
      {
        to_hot_files.push_back(abs_path);
      }
    }

    for (const auto &file : to_hot_files)
    {
      co_await migrate_to_hot_once(file);
    }
  }

  auto migrate_service() -> asio::awaitable<void>
  {
    migrate_service_timer = std::make_unique<asio::steady_timer>(co_await asio::this_coro::executor);
    while (true)
    {
      migrate_service_timer->expires_after(std::chrono::seconds{1000});
      co_await migrate_service_timer->async_wait(asio::as_tuple(asio::use_awaitable));

      LOG_INFO("start migrate service");
      co_await migrate_to_cold_service();
      co_await migrate_to_hot_service();
    }
  }

} // namespace storage_detail

namespace storage
{
  using namespace storage_detail;

  auto start_migrate_service() -> asio::awaitable<void>
  {
    init_migrate_to_cold();
    init_migrate_to_hot();
    asio::co_spawn(co_await asio::this_coro::executor, migrate_service(), common::exception_handle);
    co_return;
  }

  auto trigger_migrate_service() -> void
  {
    if (migrate_service_timer)
    {
      migrate_service_timer->cancel();
    }
  }

  auto new_hot_file(const std::string &abs_path) -> void
  {
    LOG_DEBUG(std::format("new hot file {}", abs_path));
    switch (storage_config.migrate.to_cold_rule)
    {
      case SMS_TO_COLD_DISABLE:
      {
        break;
      }
      case SMS_TO_COLD_ATIME:
      case SMS_TO_COLD_CTIME:
      {
        auto lock = std::unique_lock{hot_file_atime_or_ctime_mut};
        hot_file_atime_or_ctime[abs_path] = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
        break;
      }
    }
  }

  auto access_hot_file(const std::string &abs_path) -> void
  {
    if (storage_config.migrate.to_cold_rule != SMS_TO_COLD_ATIME)
    {
      return;
    }

    auto lock = std::unique_lock{hot_file_atime_or_ctime_mut};
    auto old_time = hot_file_atime_or_ctime[abs_path];
    auto new_time = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    hot_file_atime_or_ctime[abs_path] = new_time;
    LOG_DEBUG(std::format("access hot file {} atime {} -> {}", abs_path, old_time, new_time));
  }

  auto access_cold_file(const std::string &abs_path) -> void
  {
    if (storage_config.migrate.to_hot_rule != SMS_TO_HOT_TIMES)
    {
      return;
    }

    auto lock = std::unique_lock{cold_file_access_times_mut};
    auto old_times = cold_file_access_times[abs_path];
    auto new_times = ++old_times;
    cold_file_access_times[abs_path] = new_times;
    LOG_DEBUG(std::format("access cold file {} times {} -> {}", abs_path, old_times, new_times));
  }

  auto after_hot_to_cold(const std::string &abs_path) -> void
  {
    auto lock = std::unique_lock{hot_file_atime_or_ctime_mut};
    hot_file_atime_or_ctime.erase(abs_path);
    lock = std::unique_lock{cold_file_access_times_mut};
    cold_file_access_times[abs_path] = 0;
    LOG_DEBUG(std::format("after hot to cold {}", abs_path));
  }

  auto after_cold_to_hot(const std::string &abs_path) -> void
  {
    auto lock = std::unique_lock{cold_file_access_times_mut};
    cold_file_access_times.erase(abs_path);
    lock = std::unique_lock{hot_file_atime_or_ctime_mut};
    hot_file_atime_or_ctime[abs_path] = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    LOG_DEBUG(std::format("after cold to hot {}", abs_path));
  }

} // namespace storage
