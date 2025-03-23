module;

#include <asio.hpp>

export module master.server;

import common;
import master.config;
import master.server_util;
import master.server_for_client;

export namespace master {

  auto request_from_connection(std::shared_ptr<common::proto_frame> request, std::shared_ptr<common::connection> conn) -> asio::awaitable<void> {
    if (request == nullptr) {
      common::pop_one_connection();
      switch (conn->get_data<conn_type_t>(conn_data::conn_type).value()) {
        case conn_type_t::client:
          // co_return co_await client_disconnect(conn);
        case conn_type_t::storage:
          // co_return co_await storage_disconnect(conn);
      }
      co_return;
    }

    auto bt = common::push_one_request();
    auto info = common::request_end_info{};
    switch (conn->get_data<conn_type_t>(conn_data::conn_type).value()) {
      case conn_type_t::client: {
        info.success = co_await request_from_client(request, conn);
        break;
      }
      case conn_type_t::storage: {
        // info.success = co_await request_from_storage(request, conn);
        break;
      }
    }
    common::pop_one_request(bt, info);
  }

  auto master_server() -> asio::awaitable<void> {
    co_await common::start_metrics(std::format("{}/data/metrics.json", master_config.common.base_path));

    auto acceptor = common::acceptor{
        co_await asio::this_coro::executor,
        master_config.server.ip,
        master_config.server.port,
        master_config.server.heart_timeout,
        master_config.server.heart_interval,
    };

    while (true) {
      auto conn = co_await acceptor.accept();
      regist_client(conn);
      conn->start(request_from_connection);
      common::push_one_connection();
    }
  }

} // namespace master