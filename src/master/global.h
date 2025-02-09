#pragma once

#include "../common/log.h"
#include <asio.hpp>
#include <nlohmann/json.hpp>

extern nlohmann::json g_conf;
extern std::shared_ptr<log_t> g_log;
extern std::shared_ptr<asio::io_context> g_io_ctx;
