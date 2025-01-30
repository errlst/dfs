#pragma once
#include "../common/acceptor.h"
#include "../common/connection.h"
#include "../common/util.h"
#include "../proto/proto.h"
#include "./global.h"
#include <asio.hpp>
#include <cstdint>
#include <map>
#include <set>

/* connection 需要使用的额外数据 */
enum conn_data : uint64_t {
    /* storage 数据 */
    s_id,
    s_ip,
    s_port,
    s_magic,
    s_free_disk,
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
extern std::map<uint32_t, std::shared_ptr<connection_t>> storage_conns;
extern std::vector<std::shared_ptr<connection_t>> storage_conns_vec;
extern std::mutex storage_conns_mut;

using req_handle_t = std::function<asio::awaitable<void>(std::shared_ptr<connection_t>, std::shared_ptr<proto_frame_t>)>;

/*******************************************************************************/
/*******************************************************************************/
/* 辅助函数 */

/* 计算分组 */
auto calc_group(uint32_t id) -> uint32_t;

/* 注册 storage */
auto regist_storage(uint32_t id, std::shared_ptr<connection_t> conn) -> void;

/* 注销 storage */
auto unregist_storage(uint32_t id) -> void;

/* 循环赛获取下一个 storage */
auto next_storage() -> std::shared_ptr<connection_t>;

/* 获取组中的所有 storage */
auto group_storages(uint32_t group) -> std::set<std::shared_ptr<connection_t>>;

/*******************************************************************************/
/*******************************************************************************/
/* 服务 client 相关函数 */

extern std::map<uint8_t, req_handle_t> client_req_handles;

/* client 断连 */
auto on_client_disconnect(std::shared_ptr<connection_t> conn) -> asio::awaitable<void>;

/* 请求处理函数 */
auto cm_valid_storage_handle(std::shared_ptr<connection_t> conn, std::shared_ptr<proto_frame_t> req_frame) -> asio::awaitable<void>;
auto cm_group_storages_handle(std::shared_ptr<connection_t> conn, std::shared_ptr<proto_frame_t> req_frame) -> asio::awaitable<void>;

/* */
auto recv_from_client(std::shared_ptr<connection_t> conn) -> asio::awaitable<void>;

/*******************************************************************************/
/*******************************************************************************/
/* 服务 storage 相关函数 */

extern std::map<uint8_t, req_handle_t> storage_req_handles;

/* storage 断连 */
auto on_storage_disconnect(std::shared_ptr<connection_t> conn) -> asio::awaitable<void>;

auto sm_regist_handle(std::shared_ptr<connection_t> conn, std::shared_ptr<proto_frame_t> req_frame) -> asio::awaitable<void>;

auto recv_from_storage(std::shared_ptr<connection_t> conn) -> asio::awaitable<void>;