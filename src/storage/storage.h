#pragma once
#include "../common/log.h"
#include "store.h"
#include <string>
#include <vector>

#define COLD_DATA_MIGRATE_RULE_NONE 0
#define COLD_DATA_MIGRATE_RULE_ATIME 1
#define COLD_DATA_MIGRATE_RULE_CTIME 2

struct conf_ctx_t {
    std::string file_path;

    std::string host_ip;
    int host_port;
    int host_id;
    std::string base_path;

    std::vector<std::string> hot_store_paths;
    std::vector<std::string> warm_store_paths;
    std::vector<std::string> cold_store_paths;

    std::string master_ip;
    int master_port;

    int connect_timeout;
    int network_timeout;
    int thread_count;
    int heartbeat_interval;
    int heartbeat_timeout;
    int sync_max_delay;
    int log_level;

    int cold_data_migrate_rule;
    int cold_data_migrate_timeout;
    int cold_data_migrate_action;
    int cold_data_migrate_replica;
    int cold_data_migrate_period;
    int cold_data_migrate_timepoint;
};

struct storage_ctx_t {
    conf_ctx_t conf;
    log_t err_log; // 错误日志
    log_t acc_log; // 访问日志

    std::shared_ptr<store_ctx_group_t> hot_stores;
    std::shared_ptr<store_ctx_group_t> warm_stores;
    std::shared_ptr<store_ctx_group_t> cold_stores;
};
