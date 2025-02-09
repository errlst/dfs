#include "./global.h"

nlohmann::json g_conf;
std::string g_base_path;
std::shared_ptr<log_t> g_log;
std::shared_ptr<asio::io_context> g_io_ctx = std::make_shared<asio::io_context>();
