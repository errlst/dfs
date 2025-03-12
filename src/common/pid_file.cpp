#include "pid_file.h"
#include "log.h"
#include <filesystem>
#include <fstream>
#include <signal.h>

namespace common {
auto write_pid_file(std::string_view program, std::string_view base_path, bool force) -> void {
  auto pid_path = std::format("{}/data/pid.txt", base_path);
  if (std::filesystem::exists(pid_path)) {
    if (force) {
      auto ifs = std::ifstream{pid_path.data()};
      if (!ifs) {
        LOG_CRITICAL("failed open pid file {}", pid_path);
        exit(-1);
      }

      auto pid = 0;
      ifs >> pid;
      kill(pid, SIGQUIT);
    } else {
      LOG_CRITICAL("{} for {} is running, if not, please delete pid file {} ot run with -f", program, base_path, pid_path);
      exit(-1);
    }
  }

  auto ofs = std::ofstream{pid_path.data()};
  if (!ofs) {
    LOG_CRITICAL("failed open pid file {}", pid_path);
    exit(-1);
  }
  ofs << getpid();
  LOG_INFO("write pid to {}", pid_path);
}

auto read_pid_file(std::string_view base_path) -> int {
  auto pid_path = std::format("{}/data/pid.txt", base_path);
  if (!std::filesystem::exists(pid_path)) {
    LOG_CRITICAL("pid file {} not exists", pid_path);
    exit(-1);
  }

  auto ifs = std::ifstream{pid_path.data()};
  if (!ifs) {
    LOG_CRITICAL("failed open pid file {}", pid_path);
    exit(-1);
  }

  auto pid = 0;
  ifs >> pid;
  return pid;
}

auto remove_pid_file(std::string_view base_path) -> void {
  auto pid_path = std::format("{}/data/pid.txt", base_path);
  if (std::filesystem::exists(pid_path)) {
    std::filesystem::remove(pid_path);
    LOG_INFO("remove pid file {}", pid_path);
  }
}

} // namespace common