#include "server_for_storage.h"
#include <common/util.h>

namespace master_detail {

  auto request_storage_max_free_space(common::connection_ptr conn) -> asio::awaitable<void> {
    auto timer = asio::steady_timer{co_await asio::this_coro::executor};

    while (true) {
      auto response = co_await conn->send_request_and_wait_response({.cmd = common::proto_cmd::ms_get_max_free_space});
      if (!response) {
        co_return;
      } else if (response->stat != common::FRAME_STAT_OK) {
        LOG_ERROR("get storage free space failed, {}", response->stat);
        continue;
      }

      auto free_space = common::htonll(*(uint64_t *)response->data);
      conn->set_data<storage_max_free_space_t>(conn_data::storage_max_free_space, free_space);
      LOG_DEBUG(std::format("get storage free space {}", free_space));

      timer.expires_after(std::chrono::seconds{100});
      co_await timer.async_wait(asio::use_awaitable);
    }
  }

  auto request_storage_metrics(common::connection_ptr conn) -> asio::awaitable<void> {
    auto timer = asio::steady_timer{co_await asio::this_coro::executor};

    while (true) {
      auto response = co_await conn->send_request_and_wait_response(common::proto_frame{.cmd = common::proto_cmd::ms_get_metrics});
      if (!response) {
        co_return;
      } else if (response->stat != common::FRAME_STAT_OK) {
        LOG_ERROR("get storage metrics failed, {}", response->stat);
        continue;
      }

      auto metrics = nlohmann::json::parse(std::string_view{response->data, response->data_len}, nullptr, false);
      if (metrics.is_discarded()) {
        LOG_ERROR(std::format("parse storage metrics failed {}", std::string_view{response->data, response->data_len}));
        continue;
      }

      {
        auto lock = std::unique_lock{storage_metricses_lock};
        storage_metricses[conn] = metrics;
      }

      timer.expires_after(std::chrono::seconds{100});
      co_await timer.async_wait(asio::use_awaitable);
    }
  }

} // namespace master_detail

namespace master {

  using namespace master_detail;

  auto storage_metrics() -> nlohmann::json {
    auto ret = nlohmann::json::array();
    {
      auto lock = std::unique_lock{storage_metricses_lock};
      for (const auto &[_, metrics] : storage_metricses) {
        ret.push_back(metrics);
      }
    }
    return ret;
  }

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

    conn->add_work(request_storage_max_free_space);
    conn->add_work(request_storage_metrics);
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