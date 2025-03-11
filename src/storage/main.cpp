#include "common/log.h"
#include "common/metrics_service.h"
#include "common/pid_file.h"
#include "common/util.h"
#include "storage_service.h"
#include "storage_signal.h"
#include <print>

static auto show_usage() -> void {
  std::println("usgae: storage [options]");
  std::println("options:");
  std::println("  -h  show this help message and exit");
  std::println("  -c  specify the configuration file");
  std::println("  -d  run as a daemon");
  std::println("  -s  send signals");
  std::println("      quit");
  std::println("      sync");
  std::println("      migrate");
  exit(-1);
}

auto main(int argc, char *argv[]) -> int {
  auto config_path = std::string{};
  auto send_signal = false;
  auto signal_to_send = std::string{};
  for (auto i = 1; i < argc; ++i) {
    if (std::string_view{argv[i]} == "-c") {
      if (i + 1 >= argc) {
        show_usage();
      }
      config_path = argv[++i];
    } else if (std::string_view{argv[i]} == "-s") {
      if (i + 1 >= argc) {
        show_usage();
      }
      send_signal = true;
      signal_to_send = argv[++i];
    } else {
      show_usage();
    }
  }

  auto json = read_config(config_path);
  auto base_path = json["common"]["base_path"].get<std::string>();
  if (send_signal) {
    signal_process(signal_to_send, common::read_pid_file(base_path));
    return 0;
  }

  init_base_path(base_path);
  init_log(base_path, false, log_level::debug);
  init_signal();
  common::write_pid_file("storage", base_path);

  auto io = asio::io_context{};
  auto gurad = asio::make_work_guard(io);

  asio::co_spawn(io, storage_service(json), asio::detached);
  asio::co_spawn(io, metrics::metrics_service(json), asio::detached);

  auto thread_count = json["common"]["thread_count"].get<uint16_t>();
  for (auto i = 0; i < thread_count - 1; ++i) {
    std::thread{[&] {
      io.run();
    }}.detach();
  }
  return io.run();
}
