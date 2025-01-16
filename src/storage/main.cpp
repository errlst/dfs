#include "../common/conf.h"
#include "global.h"
#include <cstring>
#include <filesystem>
#include <print>
#include <sys/stat.h>

auto show_usage() -> void {
    std::println("usgae: storage [options]");
    std::println("options:");
    std::println("  -h\tshow this help message and exit");
    std::println("  -c\tspecify the configuration file");
    std::println("  -d\trun as a daemon");
}

auto load_conf(std::string_view path) -> void {
    conf_load_t loader;
    loader.load(path);

    auto &conf = g_storage_ctx.conf;
    conf.file_path = path;

    conf.host_ip = loader.get_or_exit<std::string>("host_ip");
    conf.host_port = loader.get_or_exit<int>("host_port");
    conf.host_id = loader.get_or_exit<int>("host_id");
    conf.base_path = loader.get_or_exit<std::string>("base_path");

    conf.master_ip = loader.get_or_exit<std::string>("master_ip");
    conf.master_port = loader.get_or_exit<int>("master_port");

    conf.connect_timeout = loader.get_or_exit<int>("connect_timeout");
    conf.network_timeout = loader.get_or_exit<int>("network_timeout");
    conf.thread_count = loader.get_or_exit<int>("thread_count");
    conf.heartbeat_interval = loader.get_or_exit<int>("heartbeat_interval");
    conf.heartbeat_timeout = loader.get_or_exit<int>("heartbeat_timeout");
    conf.sync_max_delay = loader.get_or_exit<int>("sync_max_delay");
    conf.log_level = loader.get_or_exit<int>("log_level");

    conf.cold_data_migrate_rule = loader.get_or_exit<int>("cold_data_migrate_rule");
    if (conf.cold_data_migrate_rule != COLD_DATA_MIGRATE_RULE_NONE) {
#define slove_store_paths(xxx)                                         \
    do {                                                               \
        auto store_paths =                                             \
            loader.get<std::vector<std::string>>(#xxx "_store_paths"); \
        if (store_paths->empty()) {                                    \
            conf.xxx##_store_paths = { conf.base_path + "/" #xxx };    \
        } else {                                                       \
            conf.xxx##_store_paths = std::move(store_paths.value());   \
        }                                                              \
    } while (0);
        slove_store_paths(hot);
        slove_store_paths(warm);
        slove_store_paths(cold);

        conf.cold_data_migrate_timeout = loader.get_or_exit<int>("cold_data_migrate_timeout");
        conf.cold_data_migrate_action = loader.get_or_exit<int>("cold_data_migrate_action");
        if (conf.cold_data_migrate_action == 1) {
            conf.cold_data_migrate_replica = loader.get_or_exit<int>("cold_data_migrate_replica");
        }
        conf.cold_data_migrate_period = loader.get_or_exit<int>("cold_data_migrate_period");
        conf.cold_data_migrate_timepoint = loader.get_or_exit<int>("cold_data_migrate_timepoint");
    } else {
        slove_store_paths(hot);
    }
#undef slove_store_paths
}

auto init_path() -> void {
    auto &conf = g_storage_ctx.conf;

    g_storage_ctx.hot_stores = std::make_shared<store_ctx_group_t>("hot_store_group", conf.hot_store_paths);
    g_storage_ctx.warm_stores = std::make_shared<store_ctx_group_t>("warm_store_group", conf.warm_store_paths);
    g_storage_ctx.cold_stores = std::make_shared<store_ctx_group_t>("cold_store_group", conf.cold_store_paths);
}

auto init_log(bool daemon) -> void {
    auto &conf = g_storage_ctx.conf;
    auto path = conf.base_path + "/log";
    if (std::filesystem::exists(path)) {
        if (!std::filesystem::is_directory(path)) {
            std::cerr << path << " is not a directory\n";
            exit(-1);
        }
    } else {
        if (!std::filesystem::create_directories(path)) {
            std::cerr << "failed to create directory: " << path << "\n";
            exit(-1);
        }
    }

    auto &err_log = g_storage_ctx.err_log;
    err_log.init(path + "/err.log", static_cast<log_level_e>(conf.log_level), daemon);
    err_log.log_info("err.log init suc");

    auto &acc_log = g_storage_ctx.acc_log;
    acc_log.init(path + "/acc.log", log_level_e::info, daemon);
    acc_log.log_info("suc.log init suc");
}

auto main(int argc, char *argv[]) -> int {
    // if (argc == 1) {
    //     show_usage();
    //     return 0;
    // }

    bool daemon = false;
    // for (auto i = 1; i < argc; i++) {
    //     if (std::strcmp(argv[i], "-h") == 0) {
    //         show_usage();
    //         return 0;
    //     } else if (std::strcmp(argv[i], "-c") == 0) {
    //         if (i + 1 < argc) {
    //             load_conf(argv[i + 1]);
    //         } else {
    //             std::println("missing argument for -c");
    //             return 0;
    //         }
    //     } else if (std::strcmp(argv[i], "-d") == 0) {
    //         daemon = true;
    //     }
    // }
    load_conf("/home/jin/project/dfs/conf/storage.conf");

    init_log(daemon);
    init_path();

    g_storage_ctx.hot_stores->create_file(UINT64_MAX);

    // auto paths = std::vector<std::string>{};

    // auto str = std::string{"hello"};
    // for (auto i = 0; i < 10; ++i) {
    //     auto file_id = g_storage_ctx.hot_stores->create_file(1024);
    //     g_storage_ctx.hot_stores->write_file(file_id.value(), {(uint8_t *)str.data(), str.size()});
    //     paths.emplace_back(g_storage_ctx.hot_stores->close_file(file_id.value(), std::format("file_{}", rand())).value());
    // }

    // for (auto &path : paths) {
    //     auto [file_id, idx] = g_storage_ctx.hot_stores->open_file(path).value();
    //     g_storage_ctx.hot_stores->read_file(file_id, 0, 1024).value();
    //     // std::cout << g_storage_ctx.hot_stores->read_file(file_id, 0, 1024).value() << "\n";
    // }

    std::getchar();
    return 0;
}