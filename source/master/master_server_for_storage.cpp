#include "master_server_for_storage.h"
#include <common/util.h>

namespace master_detail {

  auto request_storage_max_free_space(common::connection_ptr conn) -> asio::awaitable<void> {
    request_storage_max_free_space_timer = std::make_shared<asio::steady_timer>(co_await asio::this_coro::executor);

    while (request_storage_max_free_space_running) {
      auto response = co_await conn->send_request_and_wait_response({.cmd = common::proto_cmd::ms_get_max_free_space});
      if (!response || response->stat != common::FRAME_STAT_OK) {
        LOG_ERROR("get storage free space failed, {}", response ? response->stat : -1);
        co_return;
      }

      auto free_space = common::htonll(*(uint64_t *)response->data);
      conn->set_data<storage_max_free_space_t>(conn_data::storage_max_free_space, free_space);
      LOG_DEBUG(std::format("get storage free space {}", free_space));

      request_storage_max_free_space_timer->expires_after(std::chrono::seconds{10});
      co_await request_storage_max_free_space_timer->async_wait(asio::use_awaitable);
    }
  }

  auto stop_request_storage_max_free_space(common::connection_ptr conn) -> asio::awaitable<void> {
    request_storage_max_free_space_running = false;
    request_storage_max_free_space_timer->cancel();
    co_return;
  }

} // namespace master_detail

namespace master {

  using namespace master_detail;

  auto on_storage_disconnect(common::connection_ptr conn) -> asio::awaitable<void> {
    LOG_ERROR("storage {} disconnect", conn->address());
    unregist_storage(conn);
    co_return;
  }

  auto regist_storage(std::shared_ptr<common::connection> conn) -> void {
    conn->set_data<conn_type_t>(conn_data::conn_type, conn_type_t::storage);
    auto lock = std::unique_lock{storage_conns_lock};
    storage_conns[conn->get_data<storage_id_t>(conn_data::storage_id).value()] = conn;
    storage_conns_vec.emplace_back(conn);

    conn->add_work(request_storage_max_free_space, stop_request_storage_max_free_space);
  }

  auto unregist_storage(std::shared_ptr<common::connection> conn) -> void {
    auto lock = std::unique_lock{storage_conns_lock};
    storage_conns.erase(conn->get_data<storage_id_t>(conn_data::storage_id).value());
    storage_conns_vec = {};
    for (const auto &[_, conn] : storage_conns) {
      storage_conns_vec.emplace_back(conn);
    }
  }

  auto storage_vec() -> std::vector<std::shared_ptr<common::connection>> {
    auto lock = std::unique_lock{storage_conns_lock};
    return storage_conns_vec;
  }

  auto storage_registed(storage_id_t id) -> bool {
    auto lock = std::unique_lock{storage_conns_lock};
    return storage_conns.contains(id);
  }

  auto next_storage_round_robin() -> std::shared_ptr<common::connection> {
    static auto idx = 0ul;
    auto lock = std::unique_lock{storage_conns_lock};
    return storage_conns_vec[(idx++) % storage_conns_vec.size()];
  }

  auto storages_of_group(uint32_t group) -> std::vector<std::shared_ptr<common::connection>> {
    auto ret = std::vector<std::shared_ptr<common::connection>>{};
    for (auto i = 0u, id = master_config.server.group_size * (group - 1) + 1; i < master_config.server.group_size; ++i, ++id) {
      auto lock = std::unique_lock{storage_conns_lock};
      if (auto it = storage_conns.find(id); it != storage_conns.end()) {
        ret.push_back(it->second);
      }
    }
    return ret;
  }

} // namespace master