#include "./master_service.h"
#include "../common/acceptor.h"
#include "./master_service_handles.h"
#include "./metrics_service.h"

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

static auto master() -> asio::awaitable<void> {
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

  while (true) {
    auto conn = co_await acceptor.accept();
    conn->start(request_from_connection);
    regist_client(conn);
    metrics::push_one_connection();
  }
}

auto master_service(master_service_conf config) -> asio::awaitable<void> {
  ms_config = config;
  asio::co_spawn(co_await asio::this_coro::executor, master(), asio::detached);
  co_return;
}