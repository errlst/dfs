#include "./master_service.h"
#include "../common/acceptor.h"
#include "./master_service_handles.h"

static auto request_handles_for_client = std::map<uint16_t, request_handle>{
    {common::proto_cmd::sm_regist, sm_regist_handle},
    {common::proto_cmd::cm_fetch_one_storage, cm_fetch_one_storage_handle},
    {common::proto_cmd::cm_fetch_group_storages, cm_fetch_group_storages_handle},
};

static auto request_from_client(std::shared_ptr<common::proto_frame> request, std::shared_ptr<common::connection> conn) -> asio::awaitable<bool> {
  LOG_INFO(std::format("recv from client"));
  auto it = request_handles_for_client.find(request->cmd);
  if (it != request_handles_for_client.end()) {
    co_return co_await it->second(request, conn);
  }
  LOG_ERROR(std::format("unhandled cmd for client {}", request->cmd));
  co_return false;
}

static auto client_disconnect(std::shared_ptr<common::connection> conn) -> asio::awaitable<void> {
  LOG_INFO(std::format("client disconnect"));
  unregist_client(conn);
  co_return;
}

static auto request_handles_for_storage = std::map<uint16_t, request_handle>{};

static auto request_from_storage(std::shared_ptr<common::proto_frame> request, std::shared_ptr<common::connection> conn) -> asio::awaitable<bool> {
  LOG_INFO(std::format("recv from storage"));
  auto it = request_handles_for_storage.find(request->cmd);
  if (it != request_handles_for_storage.end()) {
    co_return co_await it->second(request, conn);
  }
  LOG_ERROR(std::format("unhandled cmd for storage {}", request->cmd));
  co_return false;
}

static auto storage_disconnect(std::shared_ptr<common::connection> conn) -> asio::awaitable<void> {
  LOG_INFO(std::format("storage disconnect"));
  unregist_storage(conn);
  co_return;
}

static auto request_from_connection(std::shared_ptr<common::proto_frame> request, std::shared_ptr<common::connection> conn) -> asio::awaitable<void> {
  if (request == nullptr) {
    metrics::pop_one_connection();
    switch (conn->get_data<uint8_t>(conn_data::type).value()) {
      case CONN_TYPE_CLIENT:
        co_return co_await client_disconnect(conn);
      case CONN_TYPE_STORAGE:
        co_return co_await storage_disconnect(conn);
    }
    co_return;
  }

  auto bt = metrics::push_one_request();
  auto info = metrics::request_end_info{};
  switch (conn->get_data<uint8_t>(conn_data::type).value()) {
    case CONN_TYPE_CLIENT: {
      info.success = co_await request_from_client(request, conn);
      break;
    }
    case CONN_TYPE_STORAGE: {
      info.success = co_await request_from_storage(request, conn);
      break;
    }
  }
  metrics::pop_one_request(bt, info);
}

/* 获取 storage metrics */
static auto storage_metrics = std::map<uint32_t, nlohmann::json>{};
static auto storgae_metrics_mut = std::mutex{};
static auto request_storage_metrics() -> asio::awaitable<void> {
  auto timer = asio::steady_timer{co_await asio::this_coro::executor};
  while (true) {
    auto storages = get_storages();
    for (auto storage : storages) {
      auto id = co_await storage->send_request({.cmd = common::proto_cmd::ms_get_metrics});
      if (!id) {
        continue;
      }

      auto response = co_await storage->recv_response(id.value());
      if (!response || response->stat != 0) {
        LOG_ERROR(std::format("get storage metrics failed {}", response ? response->stat : -1));
        continue;
      }

      auto metrics = nlohmann::json::parse(std::string_view{response->data, response->data_len}, nullptr, false);
      if (metrics.is_discarded()) {
        LOG_ERROR(std::format("parse storage metrics failed {}", std::string_view{response->data, response->data_len}));
        continue;
      }
      auto lock = std::unique_lock{storgae_metrics_mut};
      storage_metrics[storage->get_data<uint32_t>(conn_data::storage_id).value()] = metrics;
    }

    timer.expires_after(std::chrono::seconds{100});
    co_await timer.async_wait(asio::use_awaitable);
  }
}
auto master_metrics_of_storages() -> nlohmann::json {
  auto lock = std::unique_lock{storgae_metrics_mut};
  auto ret = nlohmann::json::array();
  for (const auto &[_, s] : storage_metrics) {
    ret.push_back(s);
  }
  return ret;
}

auto master_service(master_service_conf config) -> asio::awaitable<void> {
  ms_config = config;
  auto ex = co_await asio::this_coro::executor;
  auto acceptor = common::acceptor{ex,
                                   common::acceptor_conf{
                                       .ip = ms_config.ip,
                                       .port = ms_config.port,
                                       .h_timeout = ms_config.heart_timeout,
                                       .h_interval = ms_config.heart_interval,
                                   }};
  auto &io = (asio::io_context &)ex.context();
  for (auto i = 0; i < ms_config.thread_count; ++i) {
    std::thread{[&io] {
      auto guard = asio::make_work_guard(io);
      io.run();
    }}.detach();
  }

  asio::co_spawn(co_await asio::this_coro::executor, request_storage_metrics(), asio::detached);
  while (true) {
    auto conn = co_await acceptor.accept();
    conn->start(request_from_connection);
    regist_client(conn);
    metrics::push_one_connection();
  }
}
