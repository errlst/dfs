#include "common/log.h"
#include <asio.hpp>
#include <iostream>

import master;

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

  master::init_config(config_file);
  common::init_base_path(master::master_config.common.base_path);
  common::init_log(master::master_config.common.base_path, false, static_cast<common::log_level>(master::master_config.common.log_level));
  common::write_pid_file("master", master::master_config.common.base_path, true);

  auto io = asio::io_context{};
  auto gurad = asio::make_work_guard(io);

  asio::co_spawn(io, master::master_server(), common::exception_handle);

  for (auto i = 1; i < master::master_config.common.thread_count; ++i) {
    std::thread{[&io] {
      io.run();
    }}.detach();
  }

  return io.run();
}
