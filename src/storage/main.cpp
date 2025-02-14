#include "../common/log.h"
#include "./storage_service.h"
#include <fstream>
#include <random>

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
  auto config_file = "/home/jin/project/dfs/conf/storage.conf.json";
  for (auto i = 1; i < argc; ++i) {
    if (std::string_view(argv[i]) == "-h") {
      show_usage();
      return 0;
    } else if (std::string_view(argv[i]) == "-c") {
      if (i + 1 >= argc) {
        show_usage();
        return -1;
      }
      config_file = argv[i + 1];
      i += 1;
    } else {
      show_usage();
    }
  }

  auto config = read_config(config_file);

  auto io = asio::io_context{};
  asio::co_spawn(io, storage_service(storage_service_config{
                         .id = config["storage_service"]["id"].get<uint32_t>(),
                         .ip = config["storage_service"]["ip"].get<std::string>(),
                         .port = config["storage_service"]["port"].get<uint16_t>(),
                         .master_ip = config["storage_service"]["master_ip"].get<std::string>(),
                         .master_port = config["storage_service"]["master_port"].get<uint16_t>(),
                         .thread_count = config["storage_service"]["thread_count"].get<uint16_t>(),
                         .storage_magic = (uint16_t)std::random_device{}(),
                         .master_magic = config["storage_service"]["master_magic"].get<uint32_t>(),
                         .sync_interval = config["storage_service"]["sync_interval"].get<uint32_t>(),
                         .hot_paths = config["storage_service"]["hot_paths"].get<std::vector<std::string>>(),
                         .warm_paths = config["storage_service"]["warm_paths"].get<std::vector<std::string>>(),
                         .cold_paths = config["storage_service"]["cold_paths"].get<std::vector<std::string>>(),
                         .heart_timeout = config["network"]["heart_timeout"].get<uint32_t>(),
                         .heart_interval = config["network"]["heart_interval"].get<uint32_t>(),
                     }),
                 asio::detached);
  asio::co_spawn(io, metrics::metrics_service(metrics::metrics_service_config{
                         .base_path = config["common"]["base_path"].get<std::string>(),
                         .interval = 1000,
                         .extensions = {
                             {"storage_metrics", storage_metrics},
                         },
                     }),
                 asio::detached);

  auto gurad = asio::make_work_guard(io);
  return io.run();
}
