#pragma once
#include "../common/connection.h"
#include "./store.h"
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
  /**
   * @brief 连接类型 s_connection_type
   *
   */
  type,

  /**
   * @brief client 上传文件的 file_id，uint64
   *
   */
  client_upload_file_id,

  /**
   * @brief client 下载文件的 file_id，uint64
   *
   */
  client_download_file_id,

  /**
   * @brief client 下载文件的 store_ctx_group，shared_ptr<store_ctx_group>
   *
   */
  client_download_store_group,

  /**
   * @brief storage 同步文件的 file_id，uint64
   *
   */
  storage_sync_upload_file_id,
};

inline struct sss_config_ {
  uint32_t id;
  uint32_t group_id;
  std::string ip;
  uint16_t port;
  std::string master_ip;
  uint16_t master_port;
  uint16_t storage_magic;
  uint32_t master_magic;
  uint32_t sync_interval;
  std::vector<std::string> hot_paths;
  std::vector<std::string> cold_paths;
  uint32_t heart_timeout;
  uint32_t heart_interval;
} sss_config;

/************************************************************************************************************** */
/************************************************************************************************************** */

/**
 * @brief 初始化 store group
 *
 */
auto init_store_group(const std::vector<std::string> &hot_paths, const std::vector<std::string> &cold_paths) -> void;

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

/**
 * @brief 增加未同步文件
 *
 */
auto push_not_synced_file(std::string_view rel_path) -> void;

/**
 * @brief 遍历未同步文件，并移除
 *
 */
auto pop_not_synced_file() -> std::generator<std::string>;

/**
 * @brief 未同步文件数量
 *
 */
auto not_synced_file_count() -> size_t;

/************************************************************************************************************** */
/************************************************************************************************************** */

auto ms_get_max_free_space_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool>;

auto ms_get_metrics_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool>;

auto ss_regist_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool>;

auto ss_upload_sync_open_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool>;

auto ss_upload_sync_append_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool>;

auto cs_upload_open_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool>;

auto cs_upload_append_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool>;

auto cs_upload_close_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool>;

auto cs_download_open_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool>;

auto cs_download_append_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool>;
