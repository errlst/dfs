module;

#include <asio.hpp>
#include <nlohmann/json.hpp>

export module common.metrics;

export import common.metrics.request;
export import common.metrics.cpu;
export import common.metrics.mem;
export import common.metrics.net;

namespace common {

  auto metrics = nlohmann::json{};

  auto metrics_lock = std::mutex{};

  auto do_metrics(std::string out_file) -> asio::awaitable<void>;

} // namespace common

export namespace common {

  auto start_metrics(const std::string &out_file) -> asio::awaitable<void>;

  auto get_metrics() -> nlohmann::json;

} // namespace common
