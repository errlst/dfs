#include "./master_service_global.h"

conf_t conf;

std::vector<std::shared_ptr<asio::io_context>> ms_ios;
std::vector<asio::executor_work_guard<asio::io_context::executor_type>> ms_ios_guard;

std::map<uint32_t, std::shared_ptr<connection_t>> storage_conns;
std::vector<std::shared_ptr<connection_t>> storage_conns_vec;
std::mutex storage_conns_mut;

auto calc_group(uint32_t id) -> uint32_t {
    return (id - 1) / conf.group_size + 1;
}

auto regist_storage(uint32_t id, std::shared_ptr<connection_t> conn) -> void {
    auto lock = std::unique_lock{storage_conns_mut};
    storage_conns[id] = conn;
    storage_conns_vec.emplace_back(conn);
}

auto unregist_storage(uint32_t id) -> void {
    auto lock = std::unique_lock{storage_conns_mut};
    if (auto it = storage_conns.find(id); it != storage_conns.end()) {
        storage_conns.erase(it);
    }
    storage_conns_vec = {};
    for (const auto &[id, conn] : storage_conns) {
        storage_conns_vec.emplace_back(conn);
    }
}

auto next_storage() -> std::shared_ptr<connection_t> {
    static auto idx = std::atomic_uint64_t{0};
    return storage_conns_vec[idx++ % storage_conns_vec.size()];
}

auto group_storages(uint32_t group) -> std::set<std::shared_ptr<connection_t>> {
    std::set<std::shared_ptr<connection_t>> conns;
    for (auto i = 0u, id = conf.group_size * (group - 1) + 1; i < conf.group_size; ++i, ++id) {
        if (storage_conns.contains(id)) {
            if (auto it = storage_conns.find(id); it != storage_conns.end()) {
                conns.insert(it->second);
            }
        }
    }
    return conns;
}