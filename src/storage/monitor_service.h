#pragma once

#include <asio.hpp>

struct monitor_data_t {
    float sys_cpu_usage;
    float proc_cpu_usage;
    std::vector<std::tuple<std::string, uint64_t, uint64_t>> hot_store_usage;
    std::vector<std::tuple<std::string, uint64_t, uint64_t>> warm_store_usage;
    std::vector<std::tuple<std::string, uint64_t, uint64_t>> cold_store_usage;
};

auto monitor_service() -> asio::awaitable<void>;