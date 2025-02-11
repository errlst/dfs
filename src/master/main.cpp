#include "../common/log.h"
#include "./master_service.h"
#include "./metrics_service.h"
#include <fstream>
#include <nlohmann/json.hpp>

auto show_usage() -> void {
  std::println("usgae: storage [options]");
  std::println("options:");
  std::println("  -h\tshow this help message and exit");
  std::println("  -c\tspecify the configuration file");
  std::println("  -d\trun as a daemon");
}

auto read_config(std::string_view file) -> nlohmann::json {
  auto ifs = std::ifstream{file.data()};
  if (!ifs) {
    LOG_ERROR(std::format("failed to open configure : {}", file));
    exit(-1);
  }

  auto ret = nlohmann::json::parse(ifs, nullptr, false, true); // 允许 json 注释
  if (ret.empty()) {
    LOG_ERROR(std::format("parse config failed"));
    exit(-1);
  }

  if (!std::filesystem::is_directory(ret["common"]["base_path"].get<std::string>())) {
    LOG_ERROR(std::format("base_path is not a valid directory"));
    exit(-1);
  }

  return ret;
}

auto main(int argc, char *argv[]) -> int {
  auto config = read_config("/home/jin/project/dfs/conf/master.conf.json");
  auto io = asio::io_context{};
  asio::co_spawn(io, master_service(master_service_conf{
                         .ip = config["master_service"]["ip"].get<std::string>(),
                         .port = config["master_service"]["port"].get<uint16_t>(),
                         .thread_count = config["master_service"]["thread_count"].get<uint16_t>(),
                         .group_size = config["master_service"]["group_size"].get<uint16_t>(),
                         .master_magic = config["master_service"]["master_magic"].get<uint32_t>(),
                         .heart_timeout = config["network"]["heart_timeout"].get<uint32_t>(),
                         .heart_interval = config["network"]["heart_interval"].get<uint32_t>(),
                     }),
                 asio::detached);
  asio::co_spawn(io, metrics::metrics_service(metrics::metrics_service_config{
                         .base_path = config["common"]["base_path"].get<std::string>(),
                         .interval = 1000,
                     }),
                 asio::detached);
  auto guard = asio::make_work_guard(io);
  return io.run();
}
