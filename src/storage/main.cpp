#include "../common/log.h"
#include "../common/util.h"
#include "./migrate_service.h"
#include "./storage_service.h"
#include <iostream>

auto show_usage() -> void {
  std::cout << "usgae: storage [options]\n";
  std::cout << "options:\n";
  std::cout << "  -h\tshow this help message and exit\n";
  std::cout << "  -c\tspecify the configuration file\n";
  std::cout << "  -d\trun as a daemon\n";
}

auto main(int argc, char *argv[]) -> int {
  auto config_path = std::string{};
  for (auto i = 1; i < argc; ++i) {
    if (std::string_view(argv[i]) == "-h") {
      show_usage();
      return 0;
    } else if (std::string_view(argv[i]) == "-c") {
      if (i + 1 >= argc) {
        show_usage();
        return -1;
      }
      config_path = argv[i + 1];
      i += 1;
    } else {
      show_usage();
    }
  }

  auto json = read_config(config_path);
  init_base_path(json);
  init_log(json["common"]["base_path"].get<std::string>(), false, log_level::debug);

  auto io = asio::io_context{};
  asio::co_spawn(io, storage_service(json), asio::detached);
  asio::co_spawn(io, metrics::metrics_service(json, {{"storage_metrics", storage_metrics}}), asio::detached);
  // asio::co_spawn(io, metrics::metrics_service(metrics::metrics_service_config{
  //                        .base_path = json["common"]["base_path"].get<std::string>(),
  //                        .interval = 1000,
  //                        .extensions = {
  //                            {"storage_metrics", storage_metrics},
  //                        },
  //                    }),
  //                asio::detached);

  auto gurad = asio::make_work_guard(io);
  return io.run();
}
