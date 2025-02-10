#include "./master_service_global.h"

static auto recv_from_connection(std::shared_ptr<common::proto_frame> request, std::shared_ptr<common::connection> conn) -> asio::awaitable<void> {
  LOG_DEBUG(std::format("recv from connection"));

  /* 断连 */
  if (request == nullptr) {
    LOG_ERROR(std::format("connection disconnect"));
    if (conn->get_data<uint8_t>(conn_data::type).value() == conn_type_client) {
      // co_await on_client_disconnect(conn);
    } else {
      // co_await on_storage_disconnect(conn);
    }
    co_return;
  }

  /* 请求 */
  if (conn->get_data<uint8_t>(conn_data::type).value() == conn_type_client) {
    co_await recv_from_client(request, conn);
  } else {
    co_await recv_from_storage(request, conn);
  }
}

// static auto connection_disconnect(std::shared_ptr<common::connection> conn) -> asio::awaitable<void> {
//   if (conn->get_data<uint8_t>(conn_data::type).value() == 0) {
//     co_await on_client_disconnect(conn);
//   } else {
//     co_await on_storage_disconnect(conn);
//   }
// }

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
    conn->start(recv_from_connection);
    auto lock = std::unique_lock{client_conns_mut};
    client_conns.emplace(conn);
  }

  /* start accept */
  // auto acceptor = std::make_shared<acceptor_t>(co_await asio::this_coro::executor, acceptor_conf_t{
  //                                                                                          .ip = conf.ip,
  //                                                                                          .port = conf.port,
  //                                                                                          .heart_timeout = conf.heart_timeout,
  //                                                                                          .heart_interval = conf.heart_interval,
  //                                                                                          .log = g_log});
  // for (auto i = 0; i < conf.thread_count; ++i) {
  //   auto io = std::make_shared<asio::io_context>();
  //   asio::co_spawn(*io, [=] -> asio::awaitable<void> {
  //     // g_log->log_info(std::format("io start {}", (void*)io.get()));
  //     auto acceptor = std::make_shared< acceptor_t>(*io, acceptor_conf_t{
  //                                        .ip = conf.ip,
  //                                        .port = conf.port,
  //                                        .heart_timeout = conf.heart_timeout,
  //                                        .heart_interval = conf.heart_interval,
  //                                        .log = g_log});
  //     g_log->log_info(std::format("master service start on {} {}", acceptor->to_string(), (void*)io.get()));

  //     while(true){
  //       auto conn = co_await acceptor->accept();
  //       conn->start(*io);
  //       // asio::co_spawn(*io, conn->start(), asio::detached);
  //       asio::co_spawn(*io, recv_from_client(conn), asio::detached);
  //     } }, asio::detached);
  //   std::thread{
  //       [io] {
  //         auto guard = asio::make_work_guard(*io);
  //         // g_log->log_debug(std::format("run io {}", (void *)io.get()));
  //         io->run();
  //       }}
  //       .detach();
  // }
  // co_return;

  // auto acceptor = acceptor{*g_io_ctx, acceptor_conf_t{
  //                                           .ip = conf.ip,
  //                                           .port = conf.port,
  //                                           .heart_timeout = conf.heart_timeout,
  //                                           .heart_interval = conf.heart_interval,
  //                                           .log = g_log}};
  // g_m_log->log_info(std::format("master service start on {}", acceptor.to_string()));

  // /* create conn io */
  // // for (auto i = 0; i < conf.thread_count; ++i) {
  // // ms_ios.push_back(std::make_shared<asio::io_context>());
  // //   // ms_ios_guard.push_back(asio::make_work_guard(*ms_ios[i]));
  // //   std::thread{[i] {
  // //     auto guard = asio::make_work_guard(*ms_ios[i]);
  // //     ms_ios[i]->run();
  // //   }}.detach();
  // // }

  // // /* dispatch conn to io */
  // // static auto idx = 0ull;
  // while (true) {
  //   auto conn = co_await acceptor.accept();
  //   // auto io = ms_ios[idx++ % ms_ios.size()];
  //   conn->start(*m_io_ctx);
  //   // asio::co_spawn(*g_io_ctx, conn->start(), asio::detached);
  //   asio::co_spawn(*m_io_ctx, recv_from_client(conn), asio::detached);
  // }
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