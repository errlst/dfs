#include "../common/util.h"
#include "./master_service.h"
#include <iostream>

auto show_usage() -> void {
  std::cout << "usgae: storage [options]\n";
  std::cout << "options:\n";
  std::cout << "  -h\tshow this help message and exit\n";
  std::cout << "  -c\tspecify the configuration file\n";
  std::cout << "  -d\trun as a daemon\n";
}

auto main(int argc, char *argv[]) -> int {
  auto config_file = std::string{};
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
  init_base_path(config);

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
                         .extensions = {
                             {"storages_metrics", master_metrics_of_storages},
                         },
                     }),
                 asio::detached);
  auto guard = asio::make_work_guard(io);
  return io.run();
}
