#include "./storage_service_global.h"

conf_t conf;

std::shared_ptr<store_ctx_group_t> hot_stores;
std::shared_ptr<store_ctx_group_t> warm_stores;
std::shared_ptr<store_ctx_group_t> cold_stores;
std::vector<std::shared_ptr<store_ctx_group_t>> stores;

std::vector<std::shared_ptr<asio::io_context>> ss_ios;
std::vector<asio::executor_work_guard<asio::io_context::executor_type>> ss_ios_guard;

std::shared_ptr<common::connection> master_conn;

std::mutex client_conn_mut;
std::set<std::shared_ptr<common::connection>> client_conns;

std::mutex storage_conns_mut;
std::set<std::shared_ptr<common::connection>> storage_conns;

uint32_t storage_group_id = -1;

std::mutex unsync_uploaded_files_mut;
std::queue<std::string> unsync_uploaded_files;
