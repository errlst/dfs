#include "./storage_service_global.h"

conf_t conf;

std::shared_ptr<store_ctx_group_t> hot_stores;
std::shared_ptr<store_ctx_group_t> warm_stores;
std::shared_ptr<store_ctx_group_t> cold_stores;

std::vector<std::shared_ptr<asio::io_context>> ss_ios;
std::vector<asio::executor_work_guard<asio::io_context::executor_type>> ss_ios_guard;

std::shared_ptr<connection_t> master_conn;             // master 服务器连接
std::set<std::shared_ptr<connection_t>> storage_conns; // 同组 storage 服务器连接

uint32_t storage_group_id = -1;