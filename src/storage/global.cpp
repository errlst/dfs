#include "global.h"

std::shared_ptr<log_t> g_s_log;
nlohmann::json g_s_conf;
std::shared_ptr<asio::io_context> g_s_io_ctx = std::make_shared<asio::io_context>();
