#include "server.h"
#include "config.h"
#include "migrate.h"
#include "server_for_client.h"
#include "server_for_master.h"
#include "server_for_storage.h"
#include "store_util.h"
#include "sync.h"
#include <common/acceptor.h>
#include <common/metrics.h>
#include <common/metrics_request.h>

namespace storage_detail
{
  using namespace storage;

  auto storage_info_metrics() -> nlohmann::json
  {
    auto store_infos = nlohmann::json::object();
    for (auto group : store_groups())
    {
      auto infos = nlohmann::json::array();
      for (auto store : hot_store_group()->stores())
      {
        auto info = nlohmann::json::object();
        info["root_path"] = store->root_path();
        info["total_space"] = store->total_space();
        info["free_space"] = store->free_space();
        infos.push_back(info);
      }

      if (is_hot_store_group(group))
      {
        store_infos["hot"] = infos;
      }
      else
      {
        store_infos["cold"] = infos;
      }
    }

    return nlohmann::json::object({
        {"id", storage_config.server.id},
        {"ip", storage_config.server.ip},
        {"port", storage_config.server.port},
        {"base_path", storage_config.common.base_path},
        {"store_infos", store_infos},
    });
  }

  auto request_from_connection(std::shared_ptr<common::proto_frame> request, std::shared_ptr<common::connection> conn) -> asio::awaitable<void>
  {
    auto conn_type = conn->get_data<conn_type_t>(conn_data::conn_type).value();

    if (request == nullptr)
    {
      common::pop_one_connection();
      switch (conn_type)
      {
        case conn_type_t::client:
          co_await on_client_disconnect(conn);
          break;
        case conn_type_t::storage:
          co_await on_storage_disconnect(conn);
          break;
        case conn_type_t::master:
          co_await on_master_disconnect(conn);
          break;
      }
      co_return;
    }

    auto bt = common::push_one_request();
    auto ok = true;
    switch (conn_type)
    {
      case conn_type_t::client:
        ok = co_await request_from_client(request, conn);
        break;
      case conn_type_t::storage:
        ok = co_await request_from_storage(request, conn);
        break;
      case conn_type_t::master:
        ok = co_await request_from_master(request, conn);
        break;
      default:
        LOG_CRITICAL("unknown connection type {} of connection {}", static_cast<int>(conn_type), conn->address());
        break;
    }
    common::pop_one_request(bt, {.success = ok});
  }

} // namespace storage_detail

namespace storage
{

  using namespace storage_detail;

  auto storage_server() -> asio::awaitable<void>
  {
    init_store_group();
    co_await start_sync_service();
    co_await start_migrate_service();

    co_await common::start_metrics(std::format("{}/data/metrics.json", storage_config.common.base_path));
    common::add_metrics_extension({"storage_info", storage_info_metrics});

    co_await regist_to_master();

    auto acceptor = common::acceptor{co_await asio::this_coro::executor,
                                     storage_config.server.ip, (uint16_t)storage_config.server.port,
                                     storage_config.server.heart_timeout, storage_config.server.heart_interval};
    while (true)
    {
      auto conn = co_await acceptor.accept();
      regist_client(conn);
      conn->start(request_from_connection);
      common::push_one_connection();
    }
  }

} // namespace storage
