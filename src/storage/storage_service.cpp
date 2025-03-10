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
  auto it = request_handle_for_master.find(request->cmd);
  if (it != request_handle_for_master.end()) {
    co_return co_await it->second(request, conn);
  }
  LOG_ERROR("unknown request {} from master {}", common::proto_frame_to_string(*request), conn->address());
  co_return false;
}

static auto master_disconnect(std::shared_ptr<common::connection> conn) -> asio::awaitable<void> {
  LOG_CRITICAL("master {} disconnect", conn->address());
  co_return;
}

static auto request_handle_for_storage = std::map<uint16_t, request_handle>{
    {common::proto_cmd::ss_upload_sync_open, ss_upload_sync_open_handle},
    {common::proto_cmd::ss_upload_sync_append, ss_upload_sync_append_handle},
};

static auto request_from_storage(std::shared_ptr<common::proto_frame> request, std::shared_ptr<common::connection> conn) -> asio::awaitable<bool> {
  auto it = request_handle_for_storage.find(request->cmd);
  if (it != request_handle_for_storage.end()) {
    co_return co_await it->second(request, conn);
  }
  LOG_ERROR("unknown request {} from storage {}", common::proto_frame_to_string(*request), conn->address());
  co_return false;
}

static auto storage_disconnect(std::shared_ptr<common::connection> conn) -> asio::awaitable<void> {
  unregist_storage(conn);
  LOG_ERROR("storage {} disconnect", conn->address());
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
  auto it = request_handle_for_client.find(request->cmd);
  if (it != request_handle_for_client.end()) {
    co_return co_await it->second(request, conn);
  }
  LOG_ERROR("unknown request {} from client {}", common::proto_frame_to_string(*request), conn->address());
  co_return false;
}

static auto client_disconnect(std::shared_ptr<common::connection> conn) -> asio::awaitable<void> {
  unregist_client(conn);
  LOG_INFO("client {} disconnect", conn->address());
  co_return;
}

static auto request_from_connection(std::shared_ptr<common::proto_frame> request, std::shared_ptr<common::connection> conn) -> asio::awaitable<void> {
  if (request == nullptr) {
    metrics::pop_one_connection();
    switch (conn->get_data<s_conn_type>(s_conn_data::type).value()) {
      case s_conn_type::client:
        co_await client_disconnect(conn);
        break;
      case s_conn_type::storage:
        co_await storage_disconnect(conn);
        break;
      case s_conn_type::master:
        co_await master_disconnect(conn);
        break;
    }
    co_return;
  }

  auto conn_type = conn->get_data<s_conn_type>(s_conn_data::type);
  if (!conn_type) {
    LOG_CRITICAL("unknown error, conn_type invalid");
    co_return;
  }

  auto bt = metrics::push_one_request();
  auto ok = true;
  switch (conn_type.value()) {
    case s_conn_type::client:
      ok = co_await request_from_client(request, conn);
      break;
    case s_conn_type::storage:
      ok = co_await request_from_storage(request, conn);
      break;
    case s_conn_type::master:
      ok = co_await request_from_master(request, conn);
      break;
    default:
      LOG_CRITICAL("unknown connection type {} of connection {}", static_cast<int>(conn_type.value()), conn->address());
      break;
  }
  metrics::pop_one_request(bt, {.success = ok});
}

/**
 * @brief 同步上传的文件
 *
 */
static auto sync_upload_files() -> asio::awaitable<void> {
  while (true) {
    auto timer = asio::steady_timer{co_await asio::this_coro::executor, std::chrono::seconds(sss_config.sync_interval)};
    co_await timer.async_wait(asio::as_tuple(asio::use_awaitable));
    auto s_conns = registed_storages();
    if (s_conns.empty()) {
      continue;
    }

    LOG_INFO("sync file start, {} files wait for sync", not_synced_file_count());

    /* 遍历文件 */
    for (const auto &rel_path : pop_not_synced_file()) {
      auto res = hot_store_group()->open_read_file(rel_path);
      if (!res) {
        LOG_WARN("open not synced file {} failed", rel_path);
        continue;
      }
      auto [file_id, file_size, abs_path] = res.value();

      /* 请求上传 */
      auto valid_s_conns = std::set<std::shared_ptr<common::connection>>{};
      auto request_to_send = common::create_request_frame(common::proto_cmd::ss_upload_sync_open, sizeof(uint64_t) + rel_path.size());
      *(uint64_t *)request_to_send->data = htonll(file_size);
      std::copy(rel_path.begin(), rel_path.end(), request_to_send->data + sizeof(uint64_t));
      for (auto s_conn : s_conns) {
        auto response_recved = co_await s_conn->send_request_and_wait_response(request_to_send.get());
        if (!response_recved || response_recved->stat != 0) {
          LOG_ERROR("request to sync file {} failed, {}", abs_path, response_recved ? response_recved->stat : -1);
          continue;
        }
        valid_s_conns.emplace(s_conn);
      }

      /* 上传数据 */
      request_to_send = create_request_frame(common::proto_cmd::ss_upload_sync_append, 5_MB);
      while (true) {
        auto read_len = hot_store_group()->read_file(file_id, request_to_send->data, 5 * 1024 * 1024);
        if (!read_len.has_value()) {
          break;
        }
        request_to_send->data_len = read_len.value();

        for (auto s_conn : valid_s_conns) {
          auto response_recved = co_await s_conn->send_request_and_wait_response(request_to_send.get());
          if (!response_recved || response_recved->stat != 0) {
            LOG_ERROR("sync file {} append failed, {}", abs_path, response_recved ? response_recved->stat : -1);
            continue;
          }
        }

        /* 上传完成 */
        if (read_len == 0) {
          LOG_INFO("sync file {} suc", abs_path);
          break;
        }
      }
    }
  }
}

/**
 * @brief 注册到 master
 *
 */
static auto regist_to_master() -> asio::awaitable<void> {
  /* 连接到 master */
  for (auto i = 1;; i += 2) {
    auto m_conn = co_await common::connection::connect_to(sss_config.master_ip, sss_config.master_port);
    if (m_conn) {
      connected_to_master(m_conn);
      m_conn->start(request_from_connection);
      break;
    }
    LOG_ERROR(std::format("connect to master {}:{} failed", sss_config.master_ip, sss_config.master_port));
    auto timer = asio::steady_timer{co_await asio::this_coro::executor, std::chrono::seconds{i}};
    co_await timer.async_wait(asio::as_tuple(asio::use_awaitable));
  }
  LOG_INFO(std::format("connect to master suc"));

  /* 注册到 master */
  auto request_data = proto::sm_regist_request{};
  request_data.set_master_magic(sss_config.master_magic);
  request_data.mutable_s_info()->set_id(sss_config.id);
  request_data.mutable_s_info()->set_magic(sss_config.storage_magic);
  request_data.mutable_s_info()->set_port(sss_config.port);
  request_data.mutable_s_info()->set_ip("127.0.0.1");

  auto request_to_send = common::create_request_frame(common::proto_cmd::sm_regist, request_data.ByteSizeLong());
  request_data.SerializeToArray(request_to_send->data, request_to_send->data_len);
  auto response_recved = co_await master_conn()->send_request_and_wait_response(request_to_send.get());
  if (!response_recved || response_recved->stat != 0) {
    LOG_ERROR(std::format("regist to master failed {}", response_recved ? response_recved->stat : -1));
    exit(-1);
  }

  auto response_data_recved = proto::sm_regist_response{};
  if (!response_data_recved.ParseFromArray(response_recved->data, response_recved->data_len)) {
    LOG_ERROR("failed to parse sm_regist_response");
    exit(-1);
  }
  sss_config.group_id = response_data_recved.group_id();
  LOG_INFO(std::format("regist to master {} suc", master_conn()->address()));

  /* 连接到其他 storage */
  for (const auto &s_info : response_data_recved.s_infos()) {
    auto s_conn = co_await common::connection::connect_to(s_info.ip(), s_info.port());
    if (!s_conn) {
      LOG_ERROR(std::format("connect to storage {}:{} failed", s_info.ip(), s_info.port()));
      continue;
    }
    s_conn->start(request_from_connection);
    LOG_INFO(std::format("connect to storage {}:{} suc", s_info.ip(), s_info.port()));

    /* 注册到其它 storage */
    auto request_data_to_send = proto::ss_regist_request{};
    request_data_to_send.set_master_magic(sss_config.master_magic);
    request_data_to_send.set_storage_magic(s_info.magic());

    auto request_to_send = common::create_request_frame(common::ss_regist, request_data_to_send.ByteSizeLong());
    request_data_to_send.SerializeToArray(request_to_send->data, request_to_send->data_len);
    auto response_recved = co_await s_conn->send_request_and_wait_response(request_to_send.get());
    if (!response_recved || response_recved->stat != 0) {
      LOG_ERROR(std::format("regist to storage {}:{} failed {}", s_info.ip(), s_info.port(), response_recved ? response_recved->stat : -1));
      continue;
    }

    regist_storage(s_conn);
    LOG_INFO(std::format("regist to storage {}:{} suc", s_info.ip(), s_info.port()));
  }
}

auto storage_metrics() -> nlohmann::json {
  return {
      {"id", sss_config.id},
      {"group_id", sss_config.group_id},
  };
}

/**
 * @brief 初始化日志
 *
 * @param json
 * @return auto
 */
static auto init_config(const nlohmann::json &json) {
  sss_config = {
      .id = json["storage_service"]["id"].get<uint32_t>(),
      .ip = json["storage_service"]["ip"].get<std::string>(),
      .port = json["storage_service"]["port"].get<uint16_t>(),
      .master_ip = json["storage_service"]["master_ip"].get<std::string>(),
      .master_port = json["storage_service"]["master_port"].get<uint16_t>(),
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
  init_store_group(sss_config.hot_paths, sss_config.cold_paths);

  co_await regist_to_master();
  asio::co_spawn(co_await asio::this_coro::executor, sync_upload_files(), asio::detached);
  asio::co_spawn(co_await asio::this_coro::executor, migrate_service::migrate_service(json), asio::detached);
  asio::co_spawn(co_await asio::this_coro::executor, metrics::add_metrics_extension("storage_metrics", storage_metrics), asio::detached);

  auto acceptor = common::acceptor{co_await asio::this_coro::executor,
                                   common::acceptor_config{
                                       .ip = sss_config.ip,
                                       .port = sss_config.port,
                                       .h_timeout = sss_config.heart_timeout,
                                       .h_interval = sss_config.heart_interval,
                                   }};

  while (true) {
    auto conn = co_await acceptor.accept();
    regist_client(conn); // regist 需要放在 start 之前，否则可能导致接收到了消息，但还没有注册，无法识别连接类型
    conn->start(request_from_connection);
    metrics::push_one_connection();
  }
}