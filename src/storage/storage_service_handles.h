#pragma once
#include "../common/connection.h"
#include "store.h"
#include "sync_service.h"
#include <nlohmann/json.hpp>
#include <set>

using request_handle = std::function<asio::awaitable<bool>(std::shared_ptr<common::proto_frame>, std::shared_ptr<common::connection>)>;
#define REQUEST_HANDLE_PARAMS std::shared_ptr<common::proto_frame> request_recved, std::shared_ptr<common::connection> conn

/**
 * @brief 连接类型
 *
 */
enum class s_conn_type {
  client,
  storage,
  master,
};

/**
 * @brief 连接需要的额外数据
 *
 */
enum s_conn_data : uint64_t {
  x_type, // s_conn_type

  c_up_fileid, // uint64_t

  c_down_file_id,     // uint64_t
  c_down_store_group, // std::shared_ptr<store_ctx_group>

  c_down_file_path, // std::string
  c_down_file_size, // uint32_t

  s_sync_up_fileid, // uint64_t
};

/************************************************************************************************************** */
/************************************************************************************************************** */

/**
 * @brief 初始化 store group
 *
 */
auto init_store_group() -> void;

/**
 * @brief 获取 hot store group
 *
 */
auto hot_store_group() -> std::shared_ptr<store_ctx_group>;

/**
 * @brief 判断是 hot group 还是 cold group
 *
 */
auto is_hot_store_group(std::shared_ptr<store_ctx_group>) -> bool;

/**
 * @brief 获取 cold store group
 *
 */
auto cold_store_group() -> std::shared_ptr<store_ctx_group>;

/**
 * @brief 遍历 hot group 和 cold group
 *
 */
auto iterate_store_groups() -> std::generator<std::shared_ptr<store_ctx_group>>;

/************************************************************************************************************** */
/************************************************************************************************************** */

/**
 * @brief 连接到了 master。如果注册失败程序直接退出，因此该函数等价于注册到了 master
 *
 */
auto connected_to_master(std::shared_ptr<common::connection> conn) -> void;

/**
 * @brief 断开连接
 *
 */
auto disconnected_to_master() -> void;

/**
 * @brief 获取 master
 *
 */
auto master_conn() -> std::shared_ptr<common::connection>;

/************************************************************************************************************** */
/************************************************************************************************************** */

/**
 * @brief 注册 client
 *
 * @param conn
 */
auto regist_client(std::shared_ptr<common::connection> conn) -> void;

/**
 * @brief 注销 client
 *
 * @param conn
 */
auto unregist_client(std::shared_ptr<common::connection> conn) -> void;

/************************************************************************************************************** */
/************************************************************************************************************** */

/**
 * @brief 注册 storage
 *
 */
auto regist_storage(std::shared_ptr<common::connection> conn) -> void;

/**
 * @brief 注销 storage
 *
 */
auto unregist_storage(std::shared_ptr<common::connection> conn) -> void;

/**
 * @brief 获取 storage
 *
 */
auto registed_storages() -> std::set<std::shared_ptr<common::connection>>;

/************************************************************************************************************** */
/************************************************************************************************************** */

auto ms_get_max_free_space_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool>;

auto ms_get_metrics_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool>;

auto ss_regist_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool>;

auto ss_upload_sync_start_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool>;

auto ss_upload_sync_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool>;

auto cs_upload_start_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool>;

auto cs_upload_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool>;

auto cs_upload_close_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool>;

auto cs_download_start_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool>;

auto cs_download_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool>;
