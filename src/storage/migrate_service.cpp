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
} ms_config;

#define HOT_TO_COLD_DISABLE 0
#define HOT_TO_COLD_RULE_ATIME 1
#define HOT_TO_COLD_RULE_CTIME 2

#define COLD_TO_HOT_DISABLE 0
#define COLD_TO_HOT_RULE_TIMES 1

static auto init_ms_config(const nlohmann::json &json) -> void {
  ms_config = {
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

static auto hot_files_atime_or_ctime = std::map<std::string, uint64_t>{};
static auto hot_files_atime_or_ctime_mut = std::mutex{};

static auto init_migrate_to_cold(const std::vector<std::string> &paths) -> void {
  if (ms_config.to_cold_rule == 0) {
    return;
  }

  LOG_INFO("start init to cold migrate");
  struct stat st;
  for (const auto &path : paths) {
    for (const auto &file : iterate_normal_file(path)) {
      if (auto ret = stat(file.c_str(), &st); ret != 0) {
        LOG_ERROR(std::format("stat failed '{}' {}", file, ret));
      }

      if (ms_config.to_cold_rule == HOT_TO_COLD_RULE_ATIME) {
        hot_files_atime_or_ctime[file] = st.st_atime;
      } else if (ms_config.to_cold_rule == HOT_TO_COLD_RULE_CTIME) {
        hot_files_atime_or_ctime[file] = st.st_ctime;
      }
    }
  }
}

/******************************************************************************* */
/* 冷数据迁移到热数据 */
/******************************************************************************* */

static auto cold_files_times = std::map<std::string, uint64_t>{};
static auto cold_files_times_mut = std::mutex{};

static auto init_migrate_to_hot(const std::vector<std::string> &paths) -> void {
  if (ms_config.to_hot_rule == COLD_TO_HOT_DISABLE) {
    return;
  }

  LOG_INFO("start init to hot migrate");
  for (const auto &path : paths) {
    for (const auto &file : iterate_normal_file(path)) {
      cold_files_times[file] = 0;
    }
  }
}

/******************************************************************************* */
/******************************************************************************* */

namespace migrate_service {

auto start_migrate_service(const nlohmann::json &json) -> asio::awaitable<void> {
  LOG_INFO(std::format("init migrate service start"));
  init_ms_config(json);
  init_migrate_to_cold(json["storage_service"]["hot_paths"]);
  init_migrate_to_hot(json["storage_service"]["cold_paths"]);
  LOG_INFO(std::format("init migrate service suc"));
  co_return;
}

auto new_hot_file(const std::string &file) -> void {
  switch (ms_config.to_cold_rule) {
    case HOT_TO_COLD_DISABLE: {
      break;
    }
    case HOT_TO_COLD_RULE_ATIME:
    case HOT_TO_COLD_RULE_CTIME: {
      auto lock = std::unique_lock{hot_files_atime_or_ctime_mut};
      hot_files_atime_or_ctime[file] = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
      break;
    }
  }
}

} // namespace migrate_service
