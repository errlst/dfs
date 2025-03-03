#pragma once
#include "../common/connection.h"
#include "./master_service.h"
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

inline auto ms_config = master_service_conf{};

/**
 * @brief 客户连接的相关变量和函数
 *
 */
inline auto client_conns_mut = std::mutex{};
inline auto client_conns = std::set<std::shared_ptr<common::connection>>{};
inline auto regist_client(std::shared_ptr<common::connection> conn) -> void {
  conn->set_data<uint8_t>(conn_data::type, CONN_TYPE_CLIENT);
  auto lock = std::unique_lock{client_conns_mut};
  client_conns.emplace(conn);
}
inline auto unregist_client(std::shared_ptr<common::connection> conn) -> void {
  auto lock = std::unique_lock{client_conns_mut};
  client_conns.erase(conn);
}

/**
 * @brief storage 连接的相关变量和函数
 *
 */
inline auto storage_conns_mut = std::mutex{};
inline auto storage_conns = std::map<uint32_t, std::shared_ptr<common::connection>>{};
inline auto storage_conns_vec = std::vector<std::shared_ptr<common::connection>>{};
inline auto regist_storage(std::shared_ptr<common::connection> conn) -> void {
  unregist_client(conn);
  conn->set_data<uint8_t>(conn_data::type, CONN_TYPE_STORAGE);
  auto lock = std::unique_lock{storage_conns_mut};
  storage_conns[conn->get_data<uint32_t>(conn_data::storage_id).value()] = conn;
  storage_conns_vec.emplace_back(conn);
}
inline auto unregist_storage(std::shared_ptr<common::connection> conn) -> void {
  auto lock = std::unique_lock{storage_conns_mut};
  storage_conns.erase(conn->get_data<uint32_t>(conn_data::storage_id).value());
  storage_conns_vec = {};
  for (const auto &[id, conn] : storage_conns) {
    storage_conns_vec.emplace_back(conn);
  }
}
inline auto get_storages() -> std::vector<std::shared_ptr<common::connection>> {
  auto lock = std::unique_lock{storage_conns_mut};
  return storage_conns_vec;
}
inline auto storage_exists(uint32_t id) -> bool {
  auto lock = std::unique_lock{storage_conns_mut};
  return storage_conns.contains(id);
}
inline auto next_storage() -> std::shared_ptr<common::connection> {
  static auto idx = 0ul;
  auto lock = std::unique_lock{storage_conns_mut};
  return storage_conns_vec[(idx++) % storage_conns_vec.size()];
}
inline auto storage_group(uint32_t id) -> uint32_t {
  return (id - 1) / ms_config.group_size + 1;
}
inline auto group_storages(uint32_t group) -> std::vector<std::shared_ptr<common::connection>> {
  auto ret = std::vector<std::shared_ptr<common::connection>>{};
  for (auto i = 0u, id = ms_config.group_size * (group - 1) + 1; i < ms_config.group_size; ++i, ++id) {
    auto lock = std::unique_lock{storage_conns_mut};
    auto it = storage_conns.find(id);
    if (it != storage_conns.end()) {
      ret.push_back(it->second);
    }
  }
  return ret;
}

using request_handle = std::function<asio::awaitable<bool>(std::shared_ptr<common::proto_frame>, std::shared_ptr<common::connection>)>;
#define REQUEST_HANDLE_PARAMS std::shared_ptr<common::proto_frame> request_recved, std::shared_ptr<common::connection> conn

auto sm_regist_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool>;

auto cm_fetch_one_storage_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool>;

auto cm_fetch_group_storages_handle(REQUEST_HANDLE_PARAMS) -> asio::awaitable<bool>;