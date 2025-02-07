#pragma once

#include "../common/acceptor.h"
#include "../common/util.h"
#include "../proto/proto.h"
#include "./global.h"
#include "./storage_service.h"
#include "./store.h"
#include <set>

using req_handle_t = std::function<asio::awaitable<void>(std::shared_ptr<connection_t>, std::shared_ptr<proto_frame_t>)>;
#define REQ_HANDLE_PARAMS std::shared_ptr<connection_t> conn, std::shared_ptr<proto_frame_t> req_frame

enum conn_data : uint64_t {
  /* client 数据 */
  c_create_file_id,
  c_open_file_id,

};

struct conf_t {
  uint32_t id;
  std::string ip;
  uint16_t port;
  std::string master_ip;
  uint16_t master_port;
  uint16_t thread_count;
  uint16_t storage_magic;
  uint32_t master_magic;
  uint32_t sync_interval;
  std::vector<std::string> hot_paths;
  std::vector<std::string> warm_paths;
  std::vector<std::string> cold_paths;
  uint32_t heart_timeout;
  uint32_t heart_interval;
} extern conf;

extern std::shared_ptr<store_ctx_group_t> hot_stores;
extern std::shared_ptr<store_ctx_group_t> warm_stores;
extern std::shared_ptr<store_ctx_group_t> cold_stores;
extern std::vector<std::shared_ptr<store_ctx_group_t>> stores; // 方便遍历 hot、warm 和 cold

extern std::vector<std::shared_ptr<asio::io_context>> ss_ios;
extern std::vector<asio::executor_work_guard<asio::io_context::executor_type>> ss_ios_guard;

extern std::shared_ptr<connection_t> master_conn;

extern std::mutex storage_conns_mut;
extern std::set<std::shared_ptr<connection_t>> storage_conns;

extern uint32_t storage_group_id;

/************************************************************************************************************* */
/************************************************************************************************************* */
/* 服务 storage */

extern std::map<proto_cmd_e, req_handle_t> storage_req_handles;

/* 同步上传的文件 */
extern std::mutex unsync_uploaded_files_mut;
extern std::queue<std::string> unsync_uploaded_files;
auto sync_upload_files() -> asio::awaitable<void>;

/* storage 断连 */
// auto ss_sync_upload_open_handle()
auto on_storage_disconnect(std::shared_ptr<connection_t> conn) -> asio::awaitable<void>;

/* protocol 处理函数 */
auto ss_sync_upload_open_handle(REQ_HANDLE_PARAMS) -> asio::awaitable<void>;
auto ss_sync_upload_append_handle(REQ_HANDLE_PARAMS) -> asio::awaitable<void>;
auto ss_sync_upload_close(REQ_HANDLE_PARAMS) -> asio::awaitable<void>;
auto recv_from_storage(std::shared_ptr<connection_t> conn) -> asio::awaitable<void>;

/************************************************************************************************************* */
/************************************************************************************************************* */
/* 服务 client */

extern std::map<proto_cmd_e, req_handle_t> client_req_handles;

/* client 断连 */
auto on_client_disconnect(std::shared_ptr<connection_t> conn) -> asio::awaitable<void>;

/* protcol 处理函数 */
auto ss_regist_handle(std::shared_ptr<connection_t> conn, std::shared_ptr<proto_frame_t> req_frame) -> asio::awaitable<void>;
auto cs_create_file_handle(std::shared_ptr<connection_t> conn, std::shared_ptr<proto_frame_t> req_frame) -> asio::awaitable<void>;
auto cs_upload_file_handle(std::shared_ptr<connection_t> conn, std::shared_ptr<proto_frame_t> req_frame) -> asio::awaitable<void>;
auto cs_close_file_handle(std::shared_ptr<connection_t> conn, std::shared_ptr<proto_frame_t> req_frame) -> asio::awaitable<void>;
auto cs_open_file_handle(std::shared_ptr<connection_t> conn, std::shared_ptr<proto_frame_t> req_frame) -> asio::awaitable<void>;
auto cs_download_file_handle(std::shared_ptr<connection_t> conn, std::shared_ptr<proto_frame_t> req_frame) -> asio::awaitable<void>;
auto recv_from_client(std::shared_ptr<connection_t> conn) -> asio::awaitable<void>;

/************************************************************************************************************* */
/************************************************************************************************************* */
/* 服务 master */

extern std::map<proto_cmd_e, req_handle_t> master_req_handles;

/* master 断连 */
auto on_master_disconnect() -> asio::awaitable<void>;

/* 注册到 master */
auto regist_to_master() -> asio::awaitable<bool>;

/* protocol 处理函数 */
auto ms_fs_free_size_handle(std::shared_ptr<connection_t> conn, std::shared_ptr<proto_frame_t> req_frame) -> asio::awaitable<void>;
auto recv_from_master() -> asio::awaitable<void>;
