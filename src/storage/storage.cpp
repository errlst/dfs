// #include "../common/conf.h"
// #include "global.h"
// #include "master_service.h"
// #include "monitor.h"
// #include <cstring>
// #include <filesystem>
// #include <print>
// #include <sys/stat.h>

// auto show_usage() -> void {
//     std::println("usgae: storage [options]");
//     std::println("options:");
//     std::println("  -h\tshow this help message and exit");
//     std::println("  -c\tspecify the configuration file");
//     std::println("  -d\trun as a daemon");
// }

// auto init_conf(std::string_view path) -> void {
//     conf_load_t loader;
//     loader.load(path);

//     g_storage_ctx.conf = storage_conf_t{
//         .id = loader.get_or_exit<uint32_t>("id"),
//         .master_ip = loader.get_or_exit<std::string>("master_ip"),
//         .master_port = loader.get_or_exit<uint16_t>("master_port"),
//         .hot_store_paths = loader.get_or_exit<std::vector<std::string>>("hot_store_paths"),
//         .warm_store_paths = loader.get_or_exit<std::vector<std::string>>("warm_store_paths"),
//         .cold_store_paths = loader.get_or_exit<std::vector<std::string>>("cold_store_paths"),
//         .common_conf = {
//             .base_path = loader.get_or_exit<std::string>("base_path"),
//             .log_level = loader.get_or_exit<uint32_t>("log_level"),
//         },
//         .server_conf = {
//             .ip = loader.get_or_exit<std::string>("ip"),
//             .port = loader.get_or_exit<uint16_t>("port"),
//             .thread_count = loader.get_or_exit<uint16_t>("thread_count"),
//             .network_timeout = loader.get_or_exit<uint32_t>("network_timeout"),
//             .heartbeat_interval = loader.get_or_exit<uint32_t>("heartbeat_interval"),
//         },
//         .migrate_conf = {
//             .rule = loader.get_or_exit<uint32_t>("migrate_rule"),
//             .timeout = loader.get_or_exit<uint32_t>("migrate_timeout"),
//             .action = loader.get_or_exit<uint32_t>("migrate_action"),
//             .replica = loader.get_or_exit<uint32_t>("migrate_replica"),
//             .period = loader.get_or_exit<uint32_t>("migrate_period"),
//             .timepoint = loader.get_or_exit<uint32_t>("migrate_timepoint"),
//         },
//     };
// }

// auto check_path(const std::string &path) -> void {
//     if (std::filesystem::exists(path)) {
//         if (!std::filesystem::is_directory(path)) {
//             std::cerr << path << " is not a directory\n";
//             exit(-1);
//         }
//     } else {
//         if (!std::filesystem::create_directories(path)) {
//             std::cerr << "failed to create directory: " << path << "\n";
//             exit(-1);
//         }
//     }
// }

// auto init_path() -> void {
//     auto &conf = g_storage_ctx.conf;
//     g_storage_ctx.hot_stores = std::make_shared<store_ctx_group_t>("hot_store_group", conf.hot_store_paths);
//     g_storage_ctx.warm_stores = std::make_shared<store_ctx_group_t>("warm_store_group", conf.warm_store_paths);
//     g_storage_ctx.cold_stores = std::make_shared<store_ctx_group_t>("cold_store_group", conf.cold_store_paths);
//     check_path(std::format("{}/data", conf.common_conf.base_path));
// }

// auto init_log(bool daemon) -> void {
//     auto &conf = g_storage_ctx.conf;
//     auto path = conf.common_conf.base_path + "/log";
//     if (std::filesystem::exists(path)) {
//         if (!std::filesystem::is_directory(path)) {
//             std::cerr << path << " is not a directory\n";
//             exit(-1);
//         }
//     } else {
//         if (!std::filesystem::create_directories(path)) {
//             std::cerr << "failed to create directory: " << path << "\n";
//             exit(-1);
//         }
//     }

//     if (conf.common_conf.log_level >= (uint8_t)log_level_e::invalid) {
//         std::cerr << "invalid log level: " << conf.common_conf.log_level << "\n";
//         exit(-1);
//     }
//     auto log_level = static_cast<log_level_e>(conf.common_conf.log_level);
//     g_storage_ctx.err_log = std::make_shared<log_t>(path + "/err.log", log_level, daemon);
//     g_storage_ctx.err_log->log_info("err.log init suc");
//     g_storage_ctx.acc_log = std::make_shared<log_t>(path + "/acc.log", log_level, daemon);
//     g_storage_ctx.acc_log->log_info("acc.log init suc");
// }

// auto read_from_client(socket_ptr_t sock, std::shared_ptr<proto_frame_t> frame) -> asio::awaitable<void> {
//     co_return;
// }

// auto init_server() -> void {
//     g_storage_ctx.conf.server_conf.err_log = g_storage_ctx.err_log;
//     g_storage_ctx.conf.server_conf.acc_log = g_storage_ctx.acc_log;
//     g_storage_ctx.conf.server_conf.on_read = read_from_client;
//     g_storage_ctx.server = std::make_shared<server_t>(g_storage_ctx.conf.server_conf);
//     g_storage_ctx.server->regist_service(&monitor_service);
//     g_storage_ctx.server->regist_service(&master_service);
//     g_storage_ctx.server->run();
// }

// auto main(int argc, char *argv[]) -> int {
//     // if (argc == 1) {
//     //     show_usage();
//     //     return 0;
//     // }

//     bool daemon = false;
//     // for (auto i = 1; i < argc; i++) {
//     //     if (std::strcmp(argv[i], "-h") == 0) {
//     //         show_usage();
//     //         return 0;
//     //     } else if (std::strcmp(argv[i], "-c") == 0) {
//     //         if (i + 1 < argc) {
//     //             load_conf(argv[i + 1]);
//     //         } else {
//     //             std::println("missing argument for -c");
//     //             return 0;
//     //         }
//     //     } else if (std::strcmp(argv[i], "-d") == 0) {
//     //         daemon = true;
//     //     }
//     // }
//     init_conf("/home/jin/project/dfs/conf/storage.conf");
//     init_log(daemon);
//     init_path();
//     init_server();

//     return 0;
// }