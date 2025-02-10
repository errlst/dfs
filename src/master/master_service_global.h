#pragma once
#include "../common/acceptor.h"
#include "../common/connection.h"
#include "../common/util.h"
#include "../proto/proto.pb.h"
#include "./global.h"
#include <asio.hpp>
#include <cstdint>
#include <map>
#include <set>

constexpr auto CONN_TYPE_CLIENT = 0;
constexpr auto CONN_TYPE_STORAGE = 1;

enum conn_data : uint64_t {
  type, // uint8

  storage_id,         // uint32
  storage_ip,         // string
  storage_port,       // uint16
  storage_magic,      // uint32
  storage_free_space, // uint64
};

struct conf_t {
  std::string ip;
  uint16_t port;
  uint16_t thread_count;
  uint32_t master_magic;
  uint16_t group_size;
  uint32_t heart_timeout;
  uint32_t heart_interval;
} extern conf;

/* 多线程处理读消息 */
extern std::vector<std::shared_ptr<asio::io_context>> ms_ios;
extern std::vector<asio::executor_work_guard<asio::io_context::executor_type>> ms_ios_guard;

/*
    注册的 storage <id, conn>
    客户端请求 storage 时，进行循环赛，获取可用的 storage
    因此需要将 map 转换为 vector，方便进行轮询访问
    因此需要使用 mutex 保证转换时的数据安全
*/
extern std::map<uint32_t, std::shared_ptr<common::connection>> storage_conns;
extern std::vector<std::shared_ptr<common::connection>> storage_conns_vec;
extern std::mutex storage_conns_mut;

extern std::mutex client_conns_mut;
extern std::set<std::shared_ptr<common::connection>> client_conns;

using request_handle = std::function<asio::awaitable<void>(std::shared_ptr<common::proto_frame>, std::shared_ptr<common::connection>)>;
#define REQUEST_HANDLE_PARAMS std::shared_ptr<common::proto_frame> request_recved, std::shared_ptr<common::connection> conn

/*******************************************************************************/
/*******************************************************************************/
/* 辅助函数 */

/* 计算分组 */
auto storage_group(uint32_t id) -> uint32_t;

/* 获取组中的所有 storage */
auto group_storages(uint32_t group) -> std::set<std::shared_ptr<common::connection>>;

/* 注册 storage */
auto regist_storage(uint32_t id, std::shared_ptr<common::connection> conn) -> void;

auto storage_registed(uint32_t id) -> bool;

/* 注销 storage */
auto unregist_storage(uint32_t id) -> void;

/* 循环赛获取下一个 storage */
auto next_storage() -> std::shared_ptr<common::connection>;

/*******************************************************************************/
/*******************************************************************************/
/* 服务 client 相关函数 */

auto sm_regist_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<void>;

auto cm_fetch_one_storage_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<void>;

// extern std::map<uint16_t, request_handle> client_req_handles;

// // /* client 断连 */
// auto on_client_disconnect(std::shared_ptr<common::connection> conn) -> asio::awaitable<void>;

// // /* 请求处理函数 */
// // auto cm_valid_storage_handle(std::shared_ptr<common::connection> conn, std::shared_ptr<common::proto_frame> req_frame) -> asio::awaitable<void>;
// // auto cm_group_storages_handle(std::shared_ptr<common::connection> conn, std::shared_ptr<common::proto_frame> req_frame) -> asio::awaitable<void>;

// /* */
// // auto recv_from_client(std::shared_ptr<common::connection> conn) -> asio::awaitable<void>;

// // auto recv_from_client(REQUEST_HANDLE_PARAM) -> asio::awaitable<void>;

// /*******************************************************************************/
// /*******************************************************************************/
// /* 服务 storage 相关函数 */

// extern std::map<uint16_t, request_handle> storage_req_handles;

// /* storage 断连 */
// auto on_storage_disconnect(std::shared_ptr<common::connection> conn) -> asio::awaitable<void>;

// auto sm_regist_handle(REQUEST_HANDLE_PARAM) -> asio::awaitable<void>;

// // auto recv_from_storage(std::shared_ptr<common::connection> conn) -> asio::awaitable<void>;

// // auto recv_from_storage(REQUEST_HANDLE_PARAM) -> asio::awaitable<void>;
