#pragma once

#include "../common/log.h"
#include <nlohmann/json.hpp>

extern nlohmann::json g_conf;
extern std::shared_ptr<log_t> g_log;