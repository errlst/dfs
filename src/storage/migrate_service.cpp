#include "./migrate_service.h"
#include "../common/log.h"
#include "../common/util.h"
#include <sys/stat.h>

static struct {
  int to_cold_rule;
  int to_cold_timeout;
  int to_cold_action;
  int to_cold_replica;
  int to_hot_rule;
  int to_hot_times;
  int to_hot_action;
} sms_config;

#define SMS_HOT_TO_COLD_DISABLE 0
#define SMS_HOT_TO_COLD_RULE_ATIME 1
#define SMS_HOT_TO_COLD_RULE_CTIME 2
#define SMS_COLD_TO_HOT_DISABLE 0
#define SMS_COLD_TO_HOT_RULE_TIMES 1

static auto init_ms_config(const nlohmann::json &json) -> void {
  sms_config = {
      .to_cold_rule = json["migrate_service"]["to_cold_rule"].get<int>(),
      .to_cold_timeout = json["migrate_service"]["to_cold_timeout"].get<int>(),
      .to_cold_action = json["migrate_service"]["to_cold_action"].get<int>(),
      .to_cold_replica = json["migrate_service"]["to_cold_replica"].get<int>(),
      .to_hot_rule = json["migrate_service"]["to_hot_rule"].get<int>(),
      .to_hot_times = json["migrate_service"]["to_hot_times"].get<int>(),
      .to_hot_action = json["migrate_service"]["to_hot_action"].get<int>(),
  };
}

/******************************************************************************* */
/* 热数据迁移到冷数据 */
/******************************************************************************* */

static auto hot_file_atime_or_ctime = std::map<std::string, uint64_t>{};
static auto hot_file_atime_or_ctime_mut = std::mutex{};

static auto init_migrate_to_cold(const std::vector<std::string> &paths) -> void {
  if (sms_config.to_cold_rule == 0) {
    return;
  }

  LOG_INFO("to cold migrate init start");
  struct stat st;
  for (const auto &path : paths) {
    for (const auto &file : iterate_normal_file(path)) {
      if (auto ret = stat(file.c_str(), &st); ret != 0) {
        LOG_ERROR(std::format("stat failed '{}' {}", file, ret));
      }

      if (sms_config.to_cold_rule == SMS_HOT_TO_COLD_RULE_ATIME) {
        hot_file_atime_or_ctime[file] = st.st_atime;
      } else if (sms_config.to_cold_rule == SMS_HOT_TO_COLD_RULE_CTIME) {
        hot_file_atime_or_ctime[file] = st.st_ctime;
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

static auto init_migrate_to_hot(const std::vector<std::string> &paths) -> void {
  if (sms_config.to_hot_rule == SMS_COLD_TO_HOT_DISABLE) {
    return;
  }

  LOG_INFO("to hot migrate init start");
  for (const auto &path : paths) {
    for (const auto &file : iterate_normal_file(path)) {
      cold_file_access_times[file] = 0;
    }
  }
  LOG_INFO("to hot migrate init suc");
}

/******************************************************************************* */
/******************************************************************************* */

namespace migrate_service {

static auto migrate_to_cold_service() -> asio::awaitable<void> {

  co_return;
}

static auto migrate_to_hot_service() -> asio::awaitable<void> {

  co_return;
}

static auto migrate_service() -> asio::awaitable<void> {
  auto timer = asio::system_timer{co_await asio::this_coro::executor};
  while (true) {
    timer.expires_after(std::chrono::seconds{10});
    co_await migrate_to_cold_service();
    co_await migrate_to_hot_service();
    co_await timer.async_wait(asio::use_awaitable);
  }
}

auto start_migrate_service(const nlohmann::json &json) -> asio::awaitable<void> {
  init_ms_config(json);
  init_migrate_to_cold(json["storage_service"]["hot_paths"]);
  init_migrate_to_hot(json["storage_service"]["cold_paths"]);
  asio::co_spawn(co_await asio::this_coro::executor, migrate_service, asio::detached);
  co_return;
}

auto new_hot_file(const std::string &abs_path) -> void {
  LOG_DEBUG(std::format("new hot file {}", abs_path));
  switch (sms_config.to_cold_rule) {
    case SMS_HOT_TO_COLD_DISABLE: {
      break;
    }
    case SMS_HOT_TO_COLD_RULE_ATIME:
    case SMS_HOT_TO_COLD_RULE_CTIME: {
      auto lock = std::unique_lock{hot_file_atime_or_ctime_mut};
      hot_file_atime_or_ctime[abs_path] = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
      break;
    }
  }
}

auto access_hot_file(const std::string &abs_path) -> void {
  if (sms_config.to_cold_rule != SMS_HOT_TO_COLD_RULE_ATIME) {
    return;
  }
  auto lock = std::unique_lock{hot_file_atime_or_ctime_mut};
  auto old_time = hot_file_atime_or_ctime[abs_path];
  auto new_time = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
  hot_file_atime_or_ctime[abs_path] = new_time;
  LOG_DEBUG(std::format("access hot file {} atime {} -> {}", abs_path, old_time, new_time));
}

auto access_cold_file(const std::string &abs_path) -> void {
  if (sms_config.to_hot_rule != SMS_COLD_TO_HOT_RULE_TIMES) {
    return;
  }
  auto lock = std::unique_lock{cold_file_access_times_mut};
  auto old_times = cold_file_access_times[abs_path];
  auto new_times = ++old_times;
  cold_file_access_times[abs_path] = new_times;
  LOG_DEBUG(std::format("access cold file {} times {} -> {}", abs_path, old_times, new_times));
}

} // namespace migrate_service
