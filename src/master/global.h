#pragma once

#include "../common/log.h"
#include <asio.hpp>
#include <nlohmann/json.hpp>

extern nlohmann::json g_m_conf;
extern std::shared_ptr<log_t> g_m_log;
extern std::shared_ptr<asio::io_context> g_m_io_ctx;
