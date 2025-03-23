#pragma once

#include <cstdint>
#include <string>

namespace master {

  inline struct master_service_conf_t {

    struct {
      std::string base_path;
      uint8_t log_level;
      uint8_t thread_count;
    } common;

    struct {
      std::string ip;
      uint16_t port;
      uint16_t heart_timeout;
      uint16_t heart_interval;
      uint16_t group_size;
      uint32_t magic;
    } server;

  } master_config;

  /**
   * @brief 初始化配置文件
   *
   */
  auto init_config(std::string_view config_path) -> void;

} // namespace master