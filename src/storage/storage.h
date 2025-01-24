// #pragma once
// #include "../common/client.h"
// #include "../common/log.h"
// #include "../common/server.h"
// #include "migrate.h"
// #include "store.h"
// #include <string>

// #define COLD_DATA_MIGRATE_RULE_NONE 0
// #define COLD_DATA_MIGRATE_RULE_ATIME 1
// #define COLD_DATA_MIGRATE_RULE_CTIME 2

// // master 和 storage 通用的配置
// struct common_conf_t {
//     std::string base_path;
//     uint32_t log_level;
// };

// struct storage_conf_t {
//     uint32_t id;
//     std::string master_ip;
//     uint16_t master_port;
//     std::vector<std::string> hot_store_paths;
//     std::vector<std::string> warm_store_paths;
//     std::vector<std::string> cold_store_paths;
//     common_conf_t common_conf;
//     server_conf_t server_conf;
//     migrate_conf_t migrate_conf;
// };

// struct storage_ctx_t {
//     /* 作为服务器时需要的 */
//     std::string conf_file;
//     storage_conf_t conf;
//     std::shared_ptr<log_t> err_log;
//     std::shared_ptr<log_t> acc_log;
//     std::shared_ptr<store_ctx_group_t> hot_stores;
//     std::shared_ptr<store_ctx_group_t> warm_stores;
//     std::shared_ptr<store_ctx_group_t> cold_stores;
//     std::shared_ptr<server_t> server;

//     /* 作为客户端时需要的 */
//     std::shared_ptr<client_t> master;
//     uint64_t group_id;
//     std::set<std::shared_ptr<client_t>> group_storages; // 组中的其他 storage
//     client_conf_t peer_master_conf;
//     client_conf_t perr_storage_conf;
// };
