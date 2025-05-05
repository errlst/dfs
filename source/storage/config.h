#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace storage
{

  inline struct storage_config_t
  {

    struct
    {
      std::string base_path;
      uint32_t log_level;
      uint32_t thread_count;
    } common;

    struct
    {
      uint32_t id;
      std::string ip;
      uint32_t port;
      std::string master_ip;
      uint32_t master_port;
      uint32_t master_magic;
      uint32_t sync_interval;
      std::vector<std::string> hot_paths;
      std::vector<std::string> cold_paths;
      uint32_t heart_timeout;
      uint32_t heart_interval;

      struct
      {
        uint32_t storage_magic;
        uint32_t group_id;
      } internal;

    } server;

    struct
    {
      uint32_t to_cold_rule;
      uint32_t to_cold_timeout;
      uint32_t to_cold_action;
      uint32_t to_cold_replica;
      uint32_t to_hot_rule;
      uint32_t to_hot_times;
      uint32_t to_hot_action;
    } migrate;

    struct
    {
      uint32_t zero_copy_limit;
    } performance;

  } storage_config;

  /**
   * @brief 初始化配置
   *
   */
  auto init_config(std::string_view config_file) -> void;

} // namespace storage
