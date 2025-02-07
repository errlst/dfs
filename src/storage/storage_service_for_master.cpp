#include "./storage_service_global.h"

auto on_master_disconnect() -> asio::awaitable<void> {
  g_log->log_error(std::format("master {} disconnect", master_conn->to_string()));
  master_conn->close();
  master_conn = nullptr;
  co_return;
}

auto regist_to_master() -> asio::awaitable<bool> {
  /* connect to master */
  for (auto i = 1;; i *= 2) {
    master_conn = co_await connection_t::connect_to(conf.master_ip, conf.master_port, g_log);
    if (!master_conn) {
      g_log->log_error(std::format("failed to connect to master {}:{}", conf.master_ip, conf.master_port));
      co_await co_sleep_for(std::chrono::seconds{i});
      continue;
    }
    break;
  }

  /* regist to master */
  auto req_data = dfs::proto::sm_regist::request_t{};
  req_data.mutable_storage_info()->set_id(conf.id);
  req_data.mutable_storage_info()->set_port(conf.port);
  req_data.mutable_storage_info()->set_magic(conf.storage_magic);
  req_data.mutable_storage_info()->set_ip(conf.ip);
  req_data.set_master_magic(conf.master_magic);

  auto req_frame = (proto_frame_t *)malloc(sizeof(proto_frame_t) + req_data.ByteSizeLong());
  *req_frame = {
      .cmd = (uint8_t)proto_cmd_e::sm_regist,
      .data_len = (uint32_t)req_data.ByteSizeLong(),
  };
  if (!req_data.SerializeToArray(req_frame->data, req_frame->data_len)) {
    g_log->log_error("failed to serialize sm_regist request");
    co_return false;
  }
  auto id = co_await master_conn->send_req_frame(std::shared_ptr<proto_frame_t>{req_frame, [](auto p) { free(p); }});
  if (!id) {
    g_log->log_error(std::format("failed to send regist to master {}", master_conn->to_string()));
    co_return false;
  }

  /* wait regist response */
  auto res_frame = co_await master_conn->recv_res_frame(id.value());
  if (!res_frame || res_frame->cmd != (uint8_t)proto_cmd_e::sm_regist || res_frame->stat != 0) {
    g_log->log_error(std::format("failed to regist to master {} {}", master_conn->to_string(), res_frame->stat));
    co_return false;
  }

  /* parse response */
  auto res_data = dfs::proto::sm_regist::response_t{};
  if (!res_data.ParseFromArray(res_frame->data, res_frame->data_len)) {
    g_log->log_error("failed to parse sm_regist response");
    co_return false;
  }
  storage_group_id = res_data.storage_group();
  g_log->log_info(std::format("regist to master {} suc group {}", master_conn->to_string(), storage_group_id));

  /* regist to other storage */
  for (const auto &info : res_data.storage_info()) {
    /* connect to storage */
    auto conn = co_await connection_t::connect_to(info.ip(), info.port(), g_log);
    if (!conn) {
      g_log->log_error(std::format("failed to connect to storage {}:{}", info.ip(), info.port()));
      continue;
    }
    g_log->log_info(std::format("connect to storage {} {} suc", conn->to_string(), info.magic()));

    /* regist to storage */
    auto req_frame = (proto_frame_t *)malloc(sizeof(proto_frame_t) + sizeof(uint32_t));
    *req_frame = {
        .cmd = (uint8_t)proto_cmd_e::ss_regist,
        .data_len = sizeof(uint32_t),
    };
    *((uint32_t *)req_frame->data) = info.magic();

    auto id = co_await conn->send_req_frame(std::shared_ptr<proto_frame_t>{req_frame, [](auto p) { free(p); }});
    if (!id) {
      g_log->log_error(std::format("failed regist to storage {}", conn->to_string()));
      continue;
    }

    /* wait regist response */
    auto res_frame = co_await conn->recv_res_frame(id.value());
    if (!res_frame || res_frame->cmd != (uint8_t)proto_cmd_e::ss_regist || res_frame->stat != 0) {
      g_log->log_error(std::format("failed regist to storage {} {}", conn->to_string(), res_frame->stat));
      continue;
    }

    /* regist suc */
    {
      auto lock = std::unique_lock{storage_conns_mut};
      storage_conns.emplace(conn);
      g_log->log_info(std::format("regist to storage {} suc, conns size {}", conn->to_string(),
                                  storage_conns.size()));
    }
    asio::co_spawn(co_await asio::this_coro::executor, recv_from_storage(conn), asio::detached);
  }

  co_return true;
}

auto ms_fs_free_size_handle(std::shared_ptr<connection_t> conn, std::shared_ptr<proto_frame_t> req_frame) -> asio::awaitable<void> {
  auto max = (uint64_t)0;
  for (const auto &path : conf.hot_paths) {
    auto [free, total] = fs_free_size(path);
    max = std::max(max, free);
  }

  auto res_frame = (proto_frame_t *)malloc(sizeof(proto_frame_t) + sizeof(uint64_t));
  *res_frame = {
      .cmd = (uint8_t)proto_cmd_e::ms_fs_free_size,
      .data_len = sizeof(uint64_t),
  };
  *((uint64_t *)res_frame->data) = max;
  co_await conn->send_res_frame(std::shared_ptr<proto_frame_t>{res_frame, [](auto p) { free(p); }},
                                req_frame->id);
  co_return;
}

std::map<proto_cmd_e, req_handle_t> master_req_handles{
    {(proto_cmd_e)proto_cmd_e::ms_fs_free_size, ms_fs_free_size_handle},
};

auto recv_from_master() -> asio::awaitable<void> {
  /* regist to master first */
  if (!co_await regist_to_master()) {
    if (master_conn) {
      master_conn->close();
      master_conn = nullptr;
    }
    co_return;
  }

  /* recv request from master */
  while (true) {
    auto req_frame = co_await master_conn->recv_req_frame();
    if (!req_frame) {
      co_return co_await on_master_disconnect();
    }

    auto handle = master_req_handles.find((proto_cmd_e)req_frame->cmd);
    if (handle == master_req_handles.end()) {
      g_log->log_error(std::format("unhandle cmd {} from master {}", req_frame->cmd, master_conn->to_string()));
      co_await master_conn->send_res_frame(
          proto_frame_t{
              .cmd = req_frame->cmd,
              .stat = UINT8_MAX,
          },
          req_frame->id);
      continue;
    }
    co_await handle->second(master_conn, req_frame);
  }
}
