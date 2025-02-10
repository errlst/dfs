#pragma once

#include "../common/log.h"
#include <asio.hpp>
#include <nlohmann/json.hpp>

extern std::shared_ptr<log_t> g_s_log;
extern nlohmann::json g_s_conf;
extern std::shared_ptr<asio::io_context> g_s_io_ctx;
