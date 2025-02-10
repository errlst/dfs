#include "./global.h"

nlohmann::json g_m_conf;
std::string g_m_basepath;
std::shared_ptr<log_t> g_m_log;
std::shared_ptr<asio::io_context> g_m_io_ctx = std::make_shared<asio::io_context>();
