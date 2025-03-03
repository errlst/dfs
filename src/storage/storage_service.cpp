#include "../common/acceptor.h"
#include "../common/util.h"
#include "../proto/proto.pb.h"
#include "./migrate_service.h"
#include "./storage_service_handles.h"

static auto request_handle_for_master = std::map<uint16_t, request_handle>{
    {common::proto_cmd::ms_get_max_free_space, ms_get_max_free_space_handle},
    {common::proto_cmd::ms_get_metrics, ms_get_metrics_handle},
};

static auto request_from_master(std::shared_ptr<common::proto_frame> request, std::shared_ptr<common::connection> conn) -> asio::awaitable<bool> {
  // LOG_INFO(std::format("recv from master"));
  auto it = request_handle_for_master.find(request->cmd);
  if (it != request_handle_for_master.end()) {
    co_return co_await it->second(request, conn);
  }
  LOG_ERROR(std::format("unhandled cmd for master {}", request->cmd));
  co_return false;
}

static auto master_disconnect(std::shared_ptr<common::connection> conn) -> asio::awaitable<void> {
  LOG_ERROR(std::format("master disconnect"));
  co_return;
}

static auto request_handle_for_storage = std::map<uint16_t, request_handle>{
    {common::proto_cmd::ss_upload_sync_open, ss_upload_sync_open_handle},
    {common::proto_cmd::ss_upload_sync_append, ss_upload_sync_append_handle},
};

static auto request_from_storage(std::shared_ptr<common::proto_frame> request, std::shared_ptr<common::connection> conn) -> asio::awaitable<bool> {
  LOG_INFO(std::format("recv from storage"));
  auto it = request_handle_for_storage.find(request->cmd);
  if (it != request_handle_for_storage.end()) {
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

static auto request_handle_for_client = std::map<uint16_t, request_handle>{
    {common::proto_cmd::ss_regist, ss_regist_handle},
    {common::proto_cmd::cs_upload_open, cs_upload_open_handle},
    {common::proto_cmd::cs_upload_append, cs_upload_append_handle},
    {common::proto_cmd::cs_upload_close, cs_upload_close_handle},
    {common::proto_cmd::cs_download_open, cs_download_open_handle},
    {common::proto_cmd::cs_download_append, cs_download_append_handle},
};

static auto request_from_client(std::shared_ptr<common::proto_frame> request, std::shared_ptr<common::connection> conn) -> asio::awaitable<bool> {
  LOG_INFO(std::format("recv cmd {} from client", request->cmd));
  auto it = request_handle_for_client.find(request->cmd);
  if (it != request_handle_for_client.end()) {
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

static auto request_from_connection(std::shared_ptr<common::proto_frame> request, std::shared_ptr<common::connection> conn) -> asio::awaitable<void> {
  if (request == nullptr) {
    metrics::pop_one_connection();
    switch (conn->get_data<uint8_t>(conn_data::type).value()) {
      case CONN_TYPE_CLIENT:
        co_await client_disconnect(conn);
        break;
      case CONN_TYPE_STORAGE:
        co_await storage_disconnect(conn);
        break;
      case CONN_TYPE_MASTER:
        co_await master_disconnect(conn);
        break;
    }
    co_return;
  }

  LOG_DEBUG(std::format("request from connection {} {}", conn->address(), request->cmd));
  auto conn_type = conn->get_data<uint8_t>(conn_data::type);
  if (!conn_type) {
    LOG_ERROR(std::format("unknown error, conn_type invalid"));
    co_return;
  }

  auto bt = metrics::push_one_request();
  auto ok = true;
  switch (conn_type.value()) {
    case CONN_TYPE_CLIENT:
      LOG_DEBUG("client");
      ok = co_await request_from_client(request, conn);
      break;
    case CONN_TYPE_STORAGE:
      LOG_DEBUG("storage");
      ok = co_await request_from_storage(request, conn);
      break;
    case CONN_TYPE_MASTER:
      LOG_DEBUG("master");
      ok = co_await request_from_master(request, conn);
      break;
    default:
      LOG_ERROR(std::format("unknown connection type {}", conn_type.value()));
      break;
  }
  metrics::pop_one_request(bt, {.success = ok});
}

/**
 * @brief 定期同步上传的文件
 *
 */
static auto sync_upload_files() -> asio::awaitable<void> {
  while (true) {
    auto timer = asio::steady_timer{co_await asio::this_coro::executor, std::chrono::seconds(ss_config.sync_interval)};
    co_await timer.async_wait(asio::as_tuple(asio::use_awaitable));
    if (unsync_uploaded_files.empty()) {
      continue;
    }

    auto storages = get_storage_conns();
    if (storages.empty()) {
      continue;
    }

    auto file_path = get_one_unsync_uploaded_file();
    auto res = hot_store_group->open_file(file_path);
    if (!res) {
      LOG_ERROR(std::format("open file {} failed", file_path));
      continue;
    }
    auto [file_id, file_size] = res.value();

    /* 请求上传 */
    LOG_INFO(std::format("request sync upload file {}", file_path));
    auto syncable_storages = std::set<std::shared_ptr<common::connection>>{};
    auto request_to_send = std::shared_ptr<common::proto_frame>{(common::proto_frame *)malloc(sizeof(common::proto_frame) + sizeof(uint64_t) + file_path.size()), [](auto p) { free(p); }};
    *(uint64_t *)request_to_send->data = htonll(file_size);
    std::copy(file_path.begin(), file_path.end(), request_to_send->data + sizeof(uint64_t));
    for (auto storage : storages) {
      *request_to_send = {
          .cmd = common::proto_cmd::ss_upload_sync_open,
          .data_len = (uint32_t)sizeof(uint64_t) + (uint32_t)file_path.size(),
      };
      auto id = co_await storage->send_request(request_to_send.get());
      if (!id) {
        LOG_ERROR(std::format("send ss_upload_sync_open failed"));
        continue;
      }
      auto response_recved = co_await storage->recv_response(id.value());
      if (!response_recved) {
        LOG_ERROR(std::format("recv ss_upload_sync_open failed"));
        continue;
      }
      if (response_recved->stat != 0) {
        LOG_ERROR(std::format("ss_upload_sync_open failed {}", response_recved->stat));
        continue;
      }
      syncable_storages.emplace(storage);
    }

    /* 上传数据 */
    request_to_send = std::shared_ptr<common::proto_frame>{(common::proto_frame *)malloc(sizeof(common::proto_frame) + 5 * 1024 * 1024), [](auto p) { free(p); }};
    while (true) {
      auto read_len = hot_store_group->read_file(file_id, request_to_send->data, 5 * 1024 * 1024);
      if (!read_len.has_value()) {
        LOG_ERROR(std::format("read file failed"));
        exit(-1);
      }

      *request_to_send = {.cmd = common::proto_cmd::ss_upload_sync_append, .data_len = (uint32_t)read_len.value()};
      for (auto storage : syncable_storages) {
        auto id = co_await storage->send_request(request_to_send.get());
        if (!id) {
          LOG_ERROR(std::format("send ss_upload_sync_append failed"));
          continue;
        }
        auto response_recved = co_await storage->recv_response(id.value());
        if (!response_recved) {
          LOG_ERROR(std::format("recv ss_upload_sync_append failed"));
          continue;
        }
        if (response_recved->stat != 0) {
          LOG_ERROR(std::format("ss_upload_sync_append failed {}", response_recved->stat));
          continue;
        }
      }

      /* 上传完成 */
      if (read_len == 0) {
        break;
      }
    }
  }
}

static auto init_stores() -> void {
  hot_store_group = std::make_shared<store_ctx_group>("hot_store_group", ss_config.hot_paths);
  cold_store_group = std::make_shared<store_ctx_group>("cold_store_group", ss_config.cold_paths);
  store_groups = {hot_store_group, cold_store_group};
}

/**
 * @brief 注册到 master
 *
 */
static auto regist_to_master() -> asio::awaitable<bool> {
  /* connect to master */
  for (auto i = 1;; i += 2) {
    master_conn = co_await common::connection::connect_to(ss_config.master_ip, ss_config.master_port);
    if (master_conn) {
      break;
    }
    LOG_ERROR(std::format("connect to master {}:{} failed", ss_config.master_ip, ss_config.master_port));
    auto timer = asio::steady_timer{co_await asio::this_coro::executor, std::chrono::seconds{i}};
    co_await timer.async_wait(asio::as_tuple(asio::use_awaitable));
  }
  LOG_INFO(std::format("connect to master suc"));
  master_conn->set_data<uint8_t>(conn_data::type, CONN_TYPE_MASTER);
  master_conn->start(request_from_connection);

  /* regist to master */
  auto request_data = proto::sm_regist_request{};
  request_data.set_master_magic(ss_config.master_magic);
  request_data.mutable_s_info()->set_id(ss_config.id);
  request_data.mutable_s_info()->set_magic(ss_config.storage_magic);
  request_data.mutable_s_info()->set_port(ss_config.port);
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
  ss_config.group_id = response_data_recved.group_id();

  /* 连接到其他 storage */
  for (const auto &s_info : response_data_recved.s_infos()) {
    LOG_DEBUG(std::format("try connect to storage {}:{}", s_info.ip(), s_info.port()));
    auto conn = co_await common::connection::connect_to(s_info.ip(), s_info.port());
    if (!conn) {
      LOG_ERROR(std::format("connect to storage {}:{} failed", s_info.ip(), s_info.port()));
      continue;
    }
    conn->start(request_from_connection);
    LOG_DEBUG(std::format("connect to storage {}:{} suc", s_info.ip(), s_info.port()));

    /* 注册 */
    auto request_data_to_send = proto::ss_regist_request{};
    request_data_to_send.set_master_magic(ss_config.master_magic);
    request_data_to_send.set_storage_magic(s_info.magic());
    auto request_to_send = (common::proto_frame *)malloc(sizeof(common::proto_frame) + request_data_to_send.ByteSizeLong());
    *request_to_send = {
        .cmd = (uint16_t)common::proto_cmd::ss_regist,
        .data_len = (uint32_t)request_data_to_send.ByteSizeLong(),
    };
    request_data_to_send.SerializeToArray(request_to_send->data, request_to_send->data_len);
    auto id = co_await conn->send_request(request_to_send);
    free(request_to_send);
    if (!id) {
      LOG_ERROR(std::format("send ss_regist_request failed"));
      continue;
    }

    auto response_recved = co_await conn->recv_response(id.value());
    if (!response_recved) {
      LOG_ERROR(std::format("recv ss_regist_response failed"));
      continue;
    }
    if (response_recved->stat != 0) {
      LOG_ERROR(std::format("ss_regist_response failed {}", response_recved->stat));
      continue;
    }

    regist_storage(conn);
    LOG_INFO(std::format("regist to storage {}:{} suc", s_info.ip(), s_info.port()));
  }

  co_return true;
}

auto storage_metrics() -> nlohmann::json {
  return {
      {"id", ss_config.id},
      {"group_id", ss_config.group_id},
  };
}

static auto init_config(const nlohmann::json &json) {
  ss_config = storage_service_config{
      .id = json["storage_service"]["id"].get<uint32_t>(),
      .ip = json["storage_service"]["ip"].get<std::string>(),
      .port = json["storage_service"]["port"].get<uint16_t>(),
      .master_ip = json["storage_service"]["master_ip"].get<std::string>(),
      .master_port = json["storage_service"]["master_port"].get<uint16_t>(),
      .thread_count = json["storage_service"]["thread_count"].get<uint16_t>(),
      .storage_magic = (uint16_t)std::random_device{}(),
      .master_magic = json["storage_service"]["master_magic"].get<uint32_t>(),
      .sync_interval = json["storage_service"]["sync_interval"].get<uint32_t>(),
      .hot_paths = json["storage_service"]["hot_paths"].get<std::vector<std::string>>(),
      .cold_paths = json["storage_service"]["cold_paths"].get<std::vector<std::string>>(),
      .heart_timeout = json["network"]["heart_timeout"].get<uint32_t>(),
      .heart_interval = json["network"]["heart_interval"].get<uint32_t>(),
  };
}

auto storage_service(const nlohmann::json &json) -> asio::awaitable<void> {
  init_config(json);
  init_stores();
  auto ex = co_await asio::this_coro::executor;
  auto &io = (asio::io_context &)(ex.context());

  co_await migrate_service::start_migrate_service(json);

  if (!co_await regist_to_master()) {
    LOG_ERROR("regist to master failed");
    co_return;
  }
  LOG_INFO(std::format("regist to maste suc"));

  asio::co_spawn(ex, sync_upload_files(), asio::detached);

  auto acceptor = common::acceptor{ex, common::acceptor_config{
                                           .ip = ss_config.ip,
                                           .port = ss_config.port,
                                           .h_timeout = ss_config.heart_timeout,
                                           .h_interval = ss_config.heart_interval,
                                       }};
  for (auto i = 0; i < ss_config.thread_count; ++i) {
    std::thread{[&io] {
      auto guard = asio::make_work_guard(io);
      io.run();
    }}.detach();
  }

  while (true) {
    auto conn = co_await acceptor.accept();
    /* regist 需要放在 start 之前，否则可能导致接收到了消息，但还没有注册，无法识别连接类型 */
    regist_client(conn);
    conn->start(request_from_connection);
    metrics::push_one_connection();
  }
}