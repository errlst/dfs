#include "storage_config.h"
#include "common/util.h"
#include <random>

auto init_storage_config(std::string_view config_path) -> void {
  auto json = common::read_config(config_path);

  storage_config = {
      .common = {
          .base_path = json["common"]["base_path"].get<std::string>(),
          .log_level = json["common"]["log_level"].get<uint32_t>(),
          .thread_count = json["common"]["thread_count"].get<uint32_t>(),
      },

      .network = {
          .heart_timeout = json["network"]["heart_timeout"].get<uint32_t>(),
          .heart_interval = json["network"]["heart_interval"].get<uint32_t>(),
      },

      .storage_service = {
          .id = json["storage_service"]["id"].get<uint32_t>(),
          .ip = json["storage_service"]["ip"].get<std::string>(),
          .port = json["storage_service"]["port"].get<uint32_t>(),
          .master_ip = json["storage_service"]["master_ip"].get<std::string>(),
          .master_port = json["storage_service"]["master_port"].get<uint32_t>(),
          .master_magic = json["storage_service"]["master_magic"].get<uint32_t>(),
          .sync_interval = json["storage_service"]["sync_interval"].get<uint32_t>(),
          .hot_paths = json["storage_service"]["hot_paths"].get<std::vector<std::string>>(),
          .cold_paths = json["storage_service"]["cold_paths"].get<std::vector<std::string>>(),

          .internal = {
              .storage_magic = std::random_device{}(),
          },
      },

      .migrate_service = {
          .to_cold_rule = json["migrate_service"]["to_cold_rule"].get<uint32_t>(),
          .to_cold_timeout = json["migrate_service"]["to_cold_timeout"].get<uint32_t>(),
          .to_cold_action = json["migrate_service"]["to_cold_action"].get<uint32_t>(),
          .to_cold_replica = json["migrate_service"]["to_cold_replica"].get<uint32_t>(),
          .to_hot_rule = json["migrate_service"]["to_hot_rule"].get<uint32_t>(),
          .to_hot_times = json["migrate_service"]["to_hot_times"].get<uint32_t>(),
          .to_hot_action = json["migrate_service"]["to_hot_action"].get<uint32_t>(),
      },

      .metrics_service = {
          .interval = json["metrics_service"]["interval"].get<uint32_t>(),
      },

      .performance = {
          .zero_copy_limit = json["performance"]["zero_copy_limit"].get<uint32_t>(),
      }};
}
