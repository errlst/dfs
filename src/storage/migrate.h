#pragma once
#include <cstdint>

// 数据迁移配置
struct migrate_conf_t {
    uint32_t rule;
    uint32_t timeout;
    uint32_t action;
    uint32_t replica;
    uint32_t period;
    uint32_t timepoint;
};