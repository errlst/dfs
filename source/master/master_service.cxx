// module;

// #include "common/log.h"
// #include <asio.hpp>
// #include <nlohmann/json.hpp>

// export module master:service;

// export import common;
// import master_service_handles;

// export namespace master {






// /**
//  * @brief 定期获取storage 的监控信息
//  *
//  */
// auto storage_metrics = std::map<uint32_t, nlohmann::json>{};
// auto storgae_metrics_mut = std::mutex{};
// auto request_storage_metrics() -> asio::awaitable<void> {
//   auto timer = asio::steady_timer{co_await asio::this_coro::executor};
//   while (true) {
//     auto storages = get_storages();
//     for (auto storage : storages) {
//       auto id = co_await storage->send_request({.cmd = common::proto_cmd::ms_get_metrics});
//       if (!id) {
//         continue;
//       }

//       auto response = co_await storage->recv_response(id.value());
//       if (!response || response->stat != 0) {
//         LOG_ERROR(std::format("get storage metrics failed {}", response ? response->stat : -1));
//         continue;
//       }

//       auto metrics = nlohmann::json::parse(std::string_view{response->data, response->data_len}, nullptr, false);
//       if (metrics.is_discarded()) {
//         LOG_ERROR(std::format("parse storage metrics failed {}", std::string_view{response->data, response->data_len}));
//         continue;
//       }
//       auto lock = std::unique_lock{storgae_metrics_mut};
//       storage_metrics[storage->get_data<uint32_t>(s_conn_data::storage_id).value()] = metrics;
//     }

//     timer.expires_after(std::chrono::seconds{100});
//     co_await timer.async_wait(asio::as_tuple(asio::use_awaitable));
//   }
// }

// auto master_metrics_of_storages() -> nlohmann::json {
//   auto lock = std::unique_lock{storgae_metrics_mut};
//   auto ret = nlohmann::json::array();
//   for (const auto &[_, s] : storage_metrics) {
//     ret.push_back(s);
//   }
//   return ret;
// }

// auto master_service(master_service_conf config) -> asio::awaitable<void> {
//   ms_config = config;
//   auto ex = co_await asio::this_coro::executor;
//   auto acceptor = common::acceptor{
//       ex,
//       ms_config.ip,
//       ms_config.port,
//       ms_config.heart_timeout,
//       ms_config.heart_interval,
//   };
//   auto &io = (asio::io_context &)ex.context();
//   for (auto i = 0; i < ms_config.thread_count; ++i) {
//     std::thread{[&io] {
//       auto guard = asio::make_work_guard(io);
//       io.run();
//     }}.detach();
//   }

//   asio::co_spawn(co_await asio::this_coro::executor, request_storage_metrics(), asio::detached);
//   asio::co_spawn(co_await asio::this_coro::executor, common::add_metrics_extension("storages_metrics", master_metrics_of_storages), asio::detached);

//   while (true) {
//     auto conn = co_await acceptor.accept();
//     conn->start(request_from_connection);
//     regist_client(conn);
//     common::push_one_connection();
//   }
// }

// } // namespace master
