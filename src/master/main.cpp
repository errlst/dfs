#include "../common/loop.h"
#include "./global.h"
#include "./master_service.h"
#include "../common/util.h"
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

    g_conf = nlohmann::json::parse(ifs, nullptr, false, true); // 允许 json 注释
    if (g_conf.empty()) {
        std::println("failed to parse configure {}", file);
        exit(-1);
    }

    check_directory(g_conf["common"]["base_path"].get<std::string>());
}

auto init_log() -> void {
    auto base_path = g_conf["common"]["base_path"].get<std::string>();
    check_directory(std::format("{}/{}", base_path, "log"));

    auto path = std::format("{}/{}", base_path, "log/master.log");
    auto level = g_conf["common"]["log_level"].get<uint8_t>();
    g_log = std::make_shared<log_t>(path, static_cast<log_level_e>(level), false);
}

auto main(int argc, char *argv[]) -> int {
    init_conf("/home/jin/project/dfs/conf/master.conf.json");
    init_log();

    auto loop = loop_t{};
    loop.regist_service(master_service);
    loop.run();

    return 0;
}