module;

#include <asio.hpp>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

module master.server_for_storage;

namespace master {

  auto regist_storage(std::shared_ptr<common::connection> conn) -> void {
    conn->set_data<conn_type_t>(conn_data::conn_type, conn_type_t::storage);
    auto lock = std::unique_lock{storage_conns_lock};
    storage_conns[conn->get_data<storage_id_t>(conn_data::storage_id).value()] = conn;
    storage_conns_vec.emplace_back(conn);

    conn->add_work(start_request_storage_max_free_space, stop_request_storage_max_free_space);
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

  auto one_storage_round_robin() -> std::shared_ptr<common::connection> {
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