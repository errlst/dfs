#include "./storage_service_global.h"

static auto recv_from_master(std::shared_ptr<common::proto_frame> request, std::shared_ptr<common::connection> conn) -> asio::awaitable<void> {
  LOG_INFO(std::format("recv from master"));
  co_return;
}

static auto master_disconnect(std::shared_ptr<common::connection> conn) -> asio::awaitable<void> {
  LOG_INFO(std::format("master disconnect"));
  co_return;
}

static auto recv_from_storage(std::shared_ptr<common::proto_frame> request, std::shared_ptr<common::connection> conn) -> asio::awaitable<void> {
  LOG_INFO(std::format("recv from storage"));
  co_return;
}

static auto storage_disconnect(std::shared_ptr<common::connection> conn) -> asio::awaitable<void> {
  LOG_INFO(std::format("client disconnect"));
  co_return;
}

static auto recv_from_client(std::shared_ptr<common::proto_frame> request, std::shared_ptr<common::connection> conn) -> asio::awaitable<void> {
  LOG_INFO(std::format("recv from client"));
  co_return;
}

static auto client_disconnect(std::shared_ptr<common::connection> conn) -> asio::awaitable<void> {
  LOG_INFO(std::format("client disconnect"));
  co_return;
}

static auto recv_from_connection(std::shared_ptr<common::proto_frame> request, std::shared_ptr<common::connection> conn) -> asio::awaitable<void> {
  if (request == nullptr) {
    switch (conn->get_data<uint8_t>(conn_data::type).value()) {
      case conn_type_client:
        co_await client_disconnect(conn);
        break;
      case conn_type_storage:
        co_await storage_disconnect(conn);
        break;
      case conn_type_master:
        co_await master_disconnect(conn);
        break;
    }
    co_return;
  }

  switch (conn->get_data<uint8_t>(conn_data::type).value()) {
    case conn_type_client:
      co_await recv_from_client(request, conn);
      break;
    case conn_type_storage:
      co_await recv_from_storage(request, conn);
      break;
    case conn_type_master:
      co_await recv_from_master(request, conn);
      break;
  }
}

static auto regist_to_master() -> asio::awaitable<bool> {
  /* connect to master */
  for (auto i = 1;; i += 2) {
    master_conn = co_await common::connection::connect_to(conf.master_ip, conf.master_port);
    if (master_conn) {
      break;
    }
    LOG_ERROR(std::format("connect to master {}:{} failed", conf.master_ip, conf.master_port));
    co_await co_sleep_for(std::chrono::seconds(i));
  }
  LOG_INFO(std::format("connect to master suc"));
  master_conn->set_data<uint8_t>(conn_data::type, conn_type_master);
  master_conn->start(recv_from_connection);

  /* regist to master */
  auto request_data = proto::sm_regist_request{};
  request_data.set_master_magic(conf.master_magic);
  request_data.mutable_s_info()->set_id(conf.id);
  request_data.mutable_s_info()->set_magic(conf.storage_magic);
  request_data.mutable_s_info()->set_port(conf.port);
  request_data.mutable_s_info()->set_ip("127.0.0.1");
  auto request_to_send = (common::proto_frame *)malloc(sizeof(common::proto_frame) + request_data.ByteSizeLong());
  *request_to_send = {
      .cmd = (uint16_t)common::proto_cmd::sm_regist,
      .data_len = (uint32_t)request_data.ByteSizeLong(),
  };
  request_data.SerializeToArray(request_to_send->data, request_to_send->data_len);

  auto id = co_await master_conn->send_request(request_to_send);
  free(request_to_send);
  if (!id) {
    co_return false;
  }

  /* wait response */
  auto response_recved = co_await master_conn->recv_response(id.value());
  if (!response_recved) {
    co_return false;
  }
  if (response_recved->stat != 0) {
    LOG_ERROR(std::format("regist to master failed {}", response_recved->stat));
    co_return false;
  }
  auto response_data_recved = proto::sm_regist_response{};
  if (!response_data_recved.ParseFromArray(response_recved->data, response_recved->data_len)) {
    LOG_ERROR("failed to parse sm_regist_response");
    co_return false;
  }

  /* 连接到其他 storage */
  for (const auto &s_info : response_data_recved.s_infos()) {
    LOG_DEBUG(std::format("try connect to storage {}:{}", s_info.ip(), s_info.port()));
    auto conn = co_await common::connection::connect_to(s_info.ip(), s_info.port());
    if (!conn) {
      LOG_ERROR(std::format("connect to storage {}:{} failed", s_info.ip(), s_info.port()));
      continue;
    }
    LOG_DEBUG(std::format("connect to storage {}:{} suc", s_info.ip(), s_info.port()));

    /* 注册 */
  }

  co_return true;
}

static auto storage() -> asio::awaitable<void> {
  if (!co_await regist_to_master()) {
    LOG_ERROR("regist to master failed");
    co_return;
  }

  auto acceptor = common::acceptor{co_await asio::this_coro::executor,
                                   common::acceptor_conf{
                                       .ip = conf.ip,
                                       .port = conf.port,
                                       .h_timeout = conf.heart_timeout,
                                       .h_interval = conf.heart_interval,
                                   }};
  for (auto i = 0; i < conf.thread_count; ++i) {
    std::thread{[] {
      auto guard = asio::make_work_guard(*g_s_io_ctx);
      g_s_io_ctx->run();
    }}.detach();
  }

  while (true) {
    auto conn = co_await acceptor.accept();
    conn->set_data<uint8_t>(conn_data::type, conn_type_client);
    conn->start(recv_from_connection);
    auto lock = std::unique_lock{client_conn_mut};
    client_conns.emplace(conn);
  }
}

static auto init_conf() -> void {
  conf = {
      .id = g_s_conf["storage_service"]["id"].get<uint32_t>(),
      .ip = g_s_conf["storage_service"]["ip"].get<std::string>(),
      .port = g_s_conf["storage_service"]["port"].get<uint16_t>(),
      .master_ip = g_s_conf["storage_service"]["master_ip"].get<std::string>(),
      .master_port = g_s_conf["storage_service"]["master_port"].get<uint16_t>(),
      .thread_count = g_s_conf["storage_service"]["thread_count"].get<uint16_t>(),
      .storage_magic = (uint16_t)std::random_device{}(),
      .master_magic = g_s_conf["storage_service"]["master_magic"].get<uint32_t>(),
      .sync_interval = g_s_conf["storage_service"]["sync_interval"].get<uint32_t>(),
      .hot_paths = g_s_conf["storage_service"]["hot_paths"].get<std::vector<std::string>>(),
      .warm_paths = g_s_conf["storage_service"]["warm_paths"].get<std::vector<std::string>>(),
      .cold_paths = g_s_conf["storage_service"]["cold_paths"].get<std::vector<std::string>>(),
      .heart_timeout = g_s_conf["network"]["heart_timeout"].get<uint32_t>(),
      .heart_interval = g_s_conf["network"]["heart_interval"].get<uint32_t>(),
  };
}

static auto init_stores() -> void {
  hot_stores = std::make_shared<store_ctx_group_t>("hot_store_group", conf.hot_paths);
  warm_stores = std::make_shared<store_ctx_group_t>("warm_store_group", conf.warm_paths);
  cold_stores = std::make_shared<store_ctx_group_t>("cold_store_group", conf.cold_paths);
  stores = {hot_stores, warm_stores, cold_stores};
}

auto storage_service() -> asio::awaitable<void> {
  init_conf();
  init_stores();
  asio::co_spawn(co_await asio::this_coro::executor, storage(), asio::detached);
  co_return;
}