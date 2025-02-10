#include "../common/loop.h"
#include "../common/util.h"
#include "./global.h"
#include "./monitor_service.h"
#include "./storage_service.h"
#include <print>

auto show_usage() -> void {
  std::println("usgae: storage [options]");
  std::println("options:");
  std::println("  -h\tshow this help message and exit");
  std::println("  -c\tspecify the configuration file");
  std::println("  -d\trun as a daemon");
}

auto init_conf(std::string_view file) -> void {
  auto ifs = std::ifstream{file.data()};
  if (!ifs) {
    std::println("failed to open file: {}", file);
    exit(-1);
  }

  g_s_conf = nlohmann::json::parse(ifs, nullptr, false, true); // 允许 json 注释
  if (g_s_conf.empty()) {
    std::println("failed to parse configure {}", file);
    exit(-1);
  }

  check_directory(g_s_conf["common"]["base_path"].get<std::string>());
}

auto init_log() -> void {
  auto base_path = g_s_conf["common"]["base_path"].get<std::string>();
  check_directory(std::format("{}/{}", base_path, "log"));

  auto path = std::format("{}/{}", base_path, "log/master.log");
  auto level = g_s_conf["common"]["log_level"].get<uint8_t>();
  g_s_log = std::make_shared<log_t>(path, static_cast<log_level_e>(level), false);
}

auto main(int argc, char *argv[]) -> int {
  auto conf_file = "/home/jin/project/dfs/conf/storage.conf.json";

  for (auto i = 1; i < argc; ++i) {
    if (std::string_view(argv[i]) == "-h") {
      show_usage();
      return 0;
    } else if (std::string_view(argv[i]) == "-c") {
      if (i + 1 >= argc) {
        show_usage();
        return -1;
      }
      conf_file = argv[i + 1];
      i += 1;
    } else {
      show_usage();
    }
  }

  init_conf(conf_file);
  init_log();

  // asio::co_spawn(*g_s_io_ctx, monitor_service(), asio::detached);
  asio::co_spawn(*g_s_io_ctx, storage_service(), asio::detached);
  auto guard = asio::make_work_guard(g_s_io_ctx);
  g_s_io_ctx->run();

  // auto loop = loop_t{};
  // loop.regist_service(monitor_service);
  // loop.regist_service(storage_service);
  // loop.run();

  return 0;
}
