#include "master_config.h"
#include <common/util.h>

namespace master {

  auto init_config(std::string_view config_path) -> void {
    auto json = common::read_config(config_path);

    master_config = {
        .common = {
            .base_path = json["common"]["base_path"].get<std::string>(),
            .log_level = json["common"]["log_level"].get<uint8_t>(),
            .thread_count = json["common"]["thread_count"].get<uint8_t>(),
        },
        .server{
            .ip = json["server"]["ip"].get<std::string>(),
            .port = json["server"]["port"].get<uint16_t>(),
            .heart_timeout = json["server"]["heart_timeout"].get<uint16_t>(),
            .heart_interval = json["server"]["heart_interval"].get<uint16_t>(),
            .group_size = json["server"]["group_size"].get<uint16_t>(),
            .magic = json["server"]["magic"].get<uint32_t>(),
        },
    };
  }

} // namespace master