#include "./master_service_global.h"

/* 定期更新 storage 可用空间 */
static auto update_storage_free_disk_impl(std::shared_ptr<connection_t> conn) -> asio::awaitable<bool> {
  auto id = co_await conn->send_req_frame(proto_frame_t{.cmd = (uint8_t)proto_cmd_e::ms_fetch_avaliable_space});
  if (!id) {
    co_return false;
  }

  auto res_frame = co_await conn->recv_res_frame(id.value());
  if (!res_frame) {
    co_return false;
  }

  auto useable_disk = *(uint64_t *)res_frame->data;
  // g_log->log_debug(std::format("storage {} max useable disk {}", conn->to_string(), useable_disk));
  conn->set_data(conn_data_e::s_free_disk, useable_disk);
  co_return true;
}

static auto update_storage_free_disk(std::shared_ptr<connection_t> conn) -> asio::awaitable<void> {
  while (co_await update_storage_free_disk_impl(conn)) {
    co_await co_sleep_for(std::chrono::seconds{5});
  }
}

auto on_storage_disconnect(std::shared_ptr<connection_t> conn) -> asio::awaitable<void> {
  auto id = conn->get_data<uint32_t>(conn_data_e::s_id).value();
  unregist_storage(id);

  auto ip = conn->get_data<std::string>(conn_data_e::s_ip);
  auto port = conn->get_data<uint32_t>(conn_data_e::s_port);
  if (!ip.has_value() || !port.has_value()) {
    g_log->log_fatal("failed to get storage ip or port");
    exit(-1);
  }
  g_log->log_warn(std::format("storage {}:{} disconnect", ip.value(), port.value()));

  co_return;
}

auto sm_regist_handle(std::shared_ptr<connection_t> conn, std::shared_ptr<proto_frame_t> req_frame) -> asio::awaitable<void> {
  g_log->log_info(std::format("regist req from storage {}", conn->to_string()));

  /* parse request data */
  auto req_data = dfs::proto::sm_regist::request_t{};
  if (!req_data.ParseFromArray(req_frame->data, req_frame->data_len)) {
    g_log->log_warn(std::format("failed to parse sm_regist req from storage {}", conn->to_string()));
    co_await conn->send_res_frame(proto_frame_t{.stat = 1}, req_frame);
    co_return;
  }

  /* check master magic */
  if (req_data.master_magic() != conf.master_magic) {
    g_log->log_warn(std::format("invalid master magic from storage {} {}", conn->to_string(), req_data.master_magic()));
    co_await conn->send_res_frame(proto_frame_t{.stat = 2}, req_frame);
    co_return;
  }

  /* check storage id */
  if (storage_conns.contains(req_data.storage_info().id())) {
    g_log->log_warn(std::format("storage {} already regist", req_data.storage_info().id()));
    co_await conn->send_res_frame(proto_frame_t{.stat = 3}, req_frame);
    co_return;
  }

  /* response same group storage info */
  auto res_data = dfs::proto::sm_regist::response_t{};
  auto group = calc_group(req_data.storage_info().id());
  res_data.set_storage_group(group);
  for (auto storage : group_storages(group)) {
    auto info = res_data.add_storage_info();
    info->set_id(storage->get_data<uint32_t>(conn_data_e::s_id).value());
    info->set_port(storage->get_data<uint32_t>(conn_data_e::s_port).value());
    info->set_magic(storage->get_data<uint32_t>(conn_data_e::s_magic).value());
    info->set_ip(storage->get_data<std::string>(conn_data_e::s_ip).value());
  }
  auto res_frame = (proto_frame_t *)malloc(sizeof(proto_frame_t) + res_data.ByteSizeLong());
  *res_frame = {.data_len = (uint32_t)res_data.ByteSizeLong()};
  if (!res_data.SerializeToArray(res_frame->data, res_frame->data_len)) {
    g_log->log_error("failed to serialize sm_regist response");
    co_await conn->send_res_frame(proto_frame_t{.stat = 4}, req_frame);
    co_return;
  }

  /* save storage info */
  conn->set_data(conn_data_e::s_id, req_data.storage_info().id());
  conn->set_data(conn_data_e::s_port, req_data.storage_info().port());
  conn->set_data(conn_data_e::s_magic, req_data.storage_info().magic());
  conn->set_data(conn_data_e::s_ip, req_data.storage_info().ip());
  regist_storage(req_data.storage_info().id(), conn);
  g_log->log_info(std::format("storage {}:{} {} {} regist suc", req_data.storage_info().ip(), req_data.storage_info().port(), req_data.storage_info().id(), req_data.storage_info().magic()));

  co_await conn->send_res_frame(std::shared_ptr<proto_frame_t>{res_frame, [](auto p) { free(p); }}, req_frame);
  asio::co_spawn(co_await asio::this_coro::executor, recv_from_storage(conn), asio::detached);
}

std::map<uint8_t, req_handle_t> storage_req_handles{};

auto recv_from_storage(std::shared_ptr<connection_t> conn) -> asio::awaitable<void> {
  asio::co_spawn(co_await asio::this_coro::executor, update_storage_free_disk(conn), asio::detached);
  while (true) {
    auto req_frame = co_await conn->recv_req_frame();
    if (!req_frame) {
      co_await on_storage_disconnect(conn);
      co_return;
    }

    if (storage_req_handles.contains(req_frame->cmd)) {
      co_await storage_req_handles[req_frame->cmd](conn, req_frame);
    } else {
      g_log->log_error(std::format("unknown storage cmd {}", req_frame->cmd));
    }
  }
}
