#include "./master_service_global.h"

static auto request_handles_for_client = std::map<uint16_t, request_handle>{
    {common::sm_regist, sm_regist_handle},
    {common::cm_fetch_one_storage, cm_fetch_one_storage_handle},
};

static auto request_from_client(std::shared_ptr<common::proto_frame> request, std::shared_ptr<common::connection> conn) -> asio::awaitable<void> {
  LOG_INFO(std::format("recv from client"));
  auto it = request_handles_for_client.find(request->cmd);
  if (it != request_handles_for_client.end()) {
    co_return co_await it->second(request, conn);
  }
  LOG_ERROR(std::format("unhandled cmd for client {}", request->cmd));
}

static auto client_disconnect(std::shared_ptr<common::connection> conn) -> asio::awaitable<void> {
  LOG_INFO(std::format("client disconnect"));
  co_return;
}

static auto request_from_storage(std::shared_ptr<common::proto_frame> request, std::shared_ptr<common::connection> conn) -> asio::awaitable<void> {
  LOG_INFO(std::format("recv from storage"));

  co_return;
}

static auto storage_disconnect(std::shared_ptr<common::connection> conn) -> asio::awaitable<void> {
  LOG_INFO(std::format("storage disconnect"));
  co_return;
}

static auto request_from_connection(std::shared_ptr<common::proto_frame> request, std::shared_ptr<common::connection> conn) -> asio::awaitable<void> {
  if (request == nullptr) {
    switch (conn->get_data<uint8_t>(conn_data::type).value()) {
      case CONN_TYPE_CLIENT:
        co_return co_await client_disconnect(conn);
      case CONN_TYPE_STORAGE:
        co_return co_await storage_disconnect(conn);
    }
    co_return;
  }

  switch (conn->get_data<uint8_t>(conn_data::type).value()) {
    case CONN_TYPE_CLIENT:
      co_return co_await request_from_client(request, conn);
    case CONN_TYPE_STORAGE:
      co_return co_await request_from_storage(request, conn);
  }
  co_return;
}

static auto master() -> asio::awaitable<void> {
  auto acceptor = common::acceptor{co_await asio::this_coro::executor,
                                   common::acceptor_conf{
                                       .ip = conf.ip,
                                       .port = conf.port,
                                       .h_timeout = conf.heart_timeout,
                                       .h_interval = conf.heart_interval,
                                   }};
  for (auto i = 0; i < conf.thread_count; ++i) {
    std::thread{[] {
      auto guard = asio::make_work_guard(*g_m_io_ctx);
      g_m_io_ctx->run();
      LOG_ERROR(std::format("io exit"));
    }}.detach();
  }

  while (true) {
    auto conn = co_await acceptor.accept();
    conn->set_data<uint8_t>(conn_data::type, 0);
    conn->start(request_from_connection);
    auto lock = std::unique_lock{client_conns_mut};
    client_conns.emplace(conn);
  }
}

static auto init_conf() -> void {
  conf = {
      .ip = g_m_conf["master_service"]["ip"].get<std::string>(),
      .port = g_m_conf["master_service"]["port"].get<std::uint16_t>(),
      .thread_count = g_m_conf["master_service"]["thread_count"].get<std::uint16_t>(),
      .master_magic = g_m_conf["master_service"]["master_magic"].get<std::uint32_t>(),
      .group_size = g_m_conf["master_service"]["group_size"].get<std::uint16_t>(),
      .heart_timeout = g_m_conf["network"]["heart_timeout"].get<std::uint32_t>(),
      .heart_interval = g_m_conf["network"]["heart_interval"].get<std::uint32_t>(),
  };
}

auto master_service() -> asio::awaitable<void> {
  init_conf();
  asio::co_spawn(co_await asio::this_coro::executor, master(), asio::detached);
  co_return;
}