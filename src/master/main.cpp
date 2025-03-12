#include "../common/log.h"
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

  auto json = common::read_config(config_file);
  auto base_path = json["common"]["base_path"].get<std::string>();
  common::init_base_path(base_path);
  common::init_log(base_path, false);

  auto io = asio::io_context{};
  asio::co_spawn(io, master_service(master_service_conf{
                         .ip = json["master_service"]["ip"].get<std::string>(),
                         .port = json["master_service"]["port"].get<uint16_t>(),
                         .thread_count = json["master_service"]["thread_count"].get<uint16_t>(),
                         .group_size = json["master_service"]["group_size"].get<uint16_t>(),
                         .master_magic = json["master_service"]["master_magic"].get<uint32_t>(),
                         .heart_timeout = json["network"]["heart_timeout"].get<uint32_t>(),
                         .heart_interval = json["network"]["heart_interval"].get<uint32_t>(),
                     }),
                 asio::detached);

  asio::co_spawn(io, metrics::metrics_service(base_path), asio::detached);
  auto guard = asio::make_work_guard(io);
  return io.run();
}
