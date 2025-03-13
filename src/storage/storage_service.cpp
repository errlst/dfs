#include "common/acceptor.h"
#include "common/metrics_service.h"
#include "common/pid_file.h"
#include "migrate_service.h"
#include "proto/proto.pb.h"
#include "storage_config.h"
#include "sync_service.h"

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
 * @brief 注册到 master
 *
 */
static auto regist_to_master() -> asio::awaitable<void> {
  /* 连接到 master */
  for (auto i = 1;; i += 2) {
    if (i == 32) {
      exit(-1);
    }

    LOG_INFO("try connect to master {}:{}", storage_config.storage_service.master_ip, storage_config.storage_service.master_port);
    auto m_conn = co_await common::connection::connect_to(storage_config.storage_service.master_ip, storage_config.storage_service.master_port);
    if (m_conn) {
      connected_to_master(m_conn);
      m_conn->start(request_from_connection);
      break;
    }
    LOG_ERROR(std::format("connect to master {}:{} failed", storage_config.storage_service.master_ip, storage_config.storage_service.master_port));
    auto timer = asio::steady_timer{co_await asio::this_coro::executor, std::chrono::seconds{i}};
    co_await timer.async_wait(asio::as_tuple(asio::use_awaitable));
  }
  LOG_INFO(std::format("connect to master suc"));

  /* 注册到 master */
  auto request_data = proto::sm_regist_request{};
  request_data.set_master_magic(storage_config.storage_service.master_magic);
  request_data.mutable_s_info()->set_id(storage_config.storage_service.id);
  request_data.mutable_s_info()->set_magic(storage_config.storage_service.internal.storage_magic);
  request_data.mutable_s_info()->set_port(storage_config.storage_service.port);
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
  storage_config.storage_service.internal.group_id = response_data_recved.group_id();
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
    request_data_to_send.set_master_magic(storage_config.storage_service.master_magic);
    request_data_to_send.set_storage_magic(s_info.magic());

    auto request_to_send = common::create_request_frame(common::ss_regist, request_data_to_send.ByteSizeLong());
    request_data_to_send.SerializeToArray(request_to_send->data, request_to_send->data_len);
    auto response_recved = co_await s_conn->send_request_and_wait_response(request_to_send.get());
    if (!response_recved || response_recved->stat != 0) {
      LOG_ERROR(std::format("regist to storage {}:{} failed {}, with master magic 0x{:X}, storage magic 0x{:X}",
                            s_info.ip(), s_info.port(), response_recved ? response_recved->stat : -1, storage_config.storage_service.master_magic,
                            s_info.magic()));
      continue;
    }

    regist_storage(s_conn);
    LOG_INFO(std::format("regist to storage {}:{} suc", s_info.ip(), s_info.port()));
  }
}

auto storage_metrics() -> nlohmann::json {
  return {
      {"id", storage_config.storage_service.id},
      {"group_id", storage_config.storage_service.internal.group_id},
  };
}

auto storage_service() -> asio::awaitable<void> {
  init_store_group();

  co_await regist_to_master();
  asio::co_spawn(co_await asio::this_coro::executor, sync_service(), asio::detached);
  asio::co_spawn(co_await asio::this_coro::executor, migrate_service(), asio::detached);
  co_await metrics::add_metrics_extension("storage_metrics", storage_metrics);

  auto acceptor = common::acceptor{co_await asio::this_coro::executor,
                                   storage_config.storage_service.ip, (uint16_t)storage_config.storage_service.port,
                                   storage_config.network.heart_timeout, storage_config.network.heart_interval};
  while (true) {
    auto conn = co_await acceptor.accept();
    regist_client(conn); // regist 需要放在 start 之前，否则可能导致接收到了消息，但还没有注册，无法识别连接类型
    conn->start(request_from_connection);
    metrics::push_one_connection();
  }
}

auto quit_storage_service() -> void {
  common::remove_pid_file(storage_config.common.base_path);
  exit(-1);
}
