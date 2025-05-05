#include "config.h"
#include <common/util.h>
#include <random>

namespace storage
{

  auto init_config(std::string_view config_file) -> void
  {
    auto json = common::read_config(config_file);

    storage_config = {
        .common = {
            .base_path = json["common"]["base_path"].get<std::string>(),
            .log_level = json["common"]["log_level"].get<uint32_t>(),
            .thread_count = json["common"]["thread_count"].get<uint32_t>(),
        },

        .server = {
            .id = json["server"]["id"].get<uint32_t>(),
            .ip = json["server"]["ip"].get<std::string>(),
            .port = json["server"]["port"].get<uint32_t>(),
            .master_ip = json["server"]["master_ip"].get<std::string>(),
            .master_port = json["server"]["master_port"].get<uint32_t>(),
            .master_magic = json["server"]["master_magic"].get<uint32_t>(),
            .sync_interval = json["server"]["sync_interval"].get<uint32_t>(),
            .hot_paths = json["server"]["hot_paths"].get<std::vector<std::string>>(),
            .cold_paths = json["server"]["cold_paths"].get<std::vector<std::string>>(),
            .heart_timeout = json["server"]["heart_timeout"].get<uint32_t>(),
            .heart_interval = json["server"]["heart_interval"].get<uint32_t>(),

            .internal = {
                .storage_magic = std::random_device{}(),
            },
        },

        .migrate = {
            .to_cold_rule = json["migrate"]["to_cold_rule"].get<uint32_t>(),
            .to_cold_timeout = json["migrate"]["to_cold_timeout"].get<uint32_t>(),
            .to_cold_action = json["migrate"]["to_cold_action"].get<uint32_t>(),
            .to_cold_replica = json["migrate"]["to_cold_replica"].get<uint32_t>(),
            .to_hot_rule = json["migrate"]["to_hot_rule"].get<uint32_t>(),
            .to_hot_times = json["migrate"]["to_hot_times"].get<uint32_t>(),
            .to_hot_action = json["migrate"]["to_hot_action"].get<uint32_t>(),
        },

        .performance = {
            .zero_copy_limit = json["performance"]["zero_copy_limit"].get<uint32_t>(),
        },
    };
  }

} // namespace storage
