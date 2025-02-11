#pragma once
#include "../common/connection.h"
#include "../proto/proto.pb.h"
#include "./storage_service.h"
#include "./store.h"
#include <queue>
#include <set>

using request_handle = std::function<asio::awaitable<void>(std::shared_ptr<common::proto_frame>, std::shared_ptr<common::connection>)>;
#define REQUEST_HANDLE_PARAMS std::shared_ptr<common::proto_frame> request_recved, std::shared_ptr<common::connection> conn

constexpr auto CONN_TYPE_CLIENT = 0;
constexpr auto CONN_TYPE_STORAGE = 1;
constexpr auto CONN_TYPE_MASTER = 2;

enum conn_data : uint64_t {
  type,                        // uint8_t
  client_upload_file_id,       // uint64_t
  storage_sync_upload_file_id, // uint64_t
};

inline auto ss_config = storage_service_config{};

inline auto hot_stores = std::shared_ptr<store_ctx_group_t>{};
inline auto warm_stores = std::shared_ptr<store_ctx_group_t>{};
inline auto cold_stores = std::shared_ptr<store_ctx_group_t>{};
inline auto stores = std::vector<std::shared_ptr<store_ctx_group_t>>{}; // 方便遍历 hot、warm 和 cold

inline auto master_conn = std::shared_ptr<common::connection>{};

inline std::mutex client_conn_mut;
inline std::set<std::shared_ptr<common::connection>> client_conns;
inline auto regist_client(std::shared_ptr<common::connection> conn) -> void {
  conn->set_data<uint8_t>(conn_data::type, CONN_TYPE_CLIENT);
  auto lock = std::unique_lock{client_conn_mut};
  client_conns.emplace(conn);
}
inline auto unregist_client(std::shared_ptr<common::connection> conn) -> void {
  auto lock = std::unique_lock{client_conn_mut};
  client_conns.erase(conn);
}

inline uint32_t storage_group_id;
inline std::mutex storage_conns_mut;
inline std::set<std::shared_ptr<common::connection>> storage_conns;
inline auto regist_storage(std::shared_ptr<common::connection> conn) -> void {
  unregist_client(conn);
  conn->set_data<uint8_t>(conn_data::type, CONN_TYPE_STORAGE);
  auto lock = std::unique_lock{storage_conns_mut};
  storage_conns.emplace(conn);
}
inline auto unregist_storage(std::shared_ptr<common::connection> conn) -> void {
  auto lock = std::unique_lock{storage_conns_mut};
  storage_conns.erase(conn);
}
inline auto get_storage_conns() -> std::vector<std::shared_ptr<common::connection>> {
  auto ret = std::vector<std::shared_ptr<common::connection>>{};
  auto lock = std::unique_lock{storage_conns_mut};
  for (auto &conn : storage_conns) {
    ret.push_back(conn);
  }
  return ret;
}

inline std::mutex unsync_uploaded_files_mut;
inline std::queue<std::string> unsync_uploaded_files;
inline auto get_one_unsync_uploaded_file() -> std::string {
  auto lock = std::unique_lock{unsync_uploaded_files_mut};
  if (unsync_uploaded_files.empty()) {
    return "";
  }
  auto ret = unsync_uploaded_files.front();
  unsync_uploaded_files.pop();
  return ret;
}

auto ms_get_free_space_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<void>;

auto ss_regist_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<void>;

auto ss_upload_sync_open_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<void>;

auto ss_upload_sync_append_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<void>;

auto cs_upload_open_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<void>;

auto cs_upload_append_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<void>;

auto cs_upload_close_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<void>;
