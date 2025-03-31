#include "server.h"
#include "server_for_client.h"
#include <common/acceptor.h>
#include <common/metrics.h>
#include <common/metrics_request.h>

namespace master {

  auto request_from_connection(std::shared_ptr<common::proto_frame> request, std::shared_ptr<common::connection> conn) -> asio::awaitable<void> {
    if (request == nullptr) {
      common::pop_one_connection();
      switch (conn->get_data<conn_type_t>(conn_data::conn_type).value()) {
        case conn_type_t::client: {
          co_await on_client_disconnect(conn);
          break;
        }
        case conn_type_t::storage: {
          co_await on_storage_disconnect(conn);
          break;
        }
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
        LOG_CRITICAL("TODO");
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