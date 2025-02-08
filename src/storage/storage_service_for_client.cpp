#include "./storage_service_global.h"

auto on_client_disconnect(std::shared_ptr<connection_t> conn) -> asio::awaitable<void> {
  g_log->log_info(std::format("client {} disconnect", conn->to_string()));
  co_return;
}

auto ss_regist_handle(REQ_HANDLE_PARAMS) -> asio::awaitable<void> {
  if (req_frame->data_len != sizeof(uint32_t)) {
    g_log->log_error(std::format("invalid ss_regist data_len {}", (uint32_t)req_frame->data_len));
    co_return;
  }

  if (*(uint32_t *)req_frame->data != conf.storage_magic) {
    g_log->log_error(std::format("invalid ss_regist magic {} {}", conf.storage_magic, *(uint32_t *)req_frame->data));
    co_return;
  }

  /* regist suc */
  if (!co_await conn->send_res_frame(proto_frame_t{}, req_frame)) {
    co_return co_await on_client_disconnect(conn);
  }
  {
    auto lock = std::lock_guard{storage_conns_mut};
    storage_conns.insert(conn);
    g_log->log_info(std::format("storage {} regist suc, storage conns size {}", conn->to_string(),
                                storage_conns.size()));
  }
  asio::co_spawn(co_await asio::this_coro::executor, recv_from_storage(conn), asio::detached);
}

auto cs_upload_create_handle(REQ_HANDLE_PARAMS) -> asio::awaitable<void> {
  if (conn->has_data((uint64_t)conn_data_e::c_upload_file_id)) {
    g_log->log_error(std::format("file already created {}", conn->to_string()));
    co_await conn->send_res_frame(proto_frame_t{.stat = 1}, req_frame);
    co_return;
  }

  if (req_frame->data_len != sizeof(uint64_t)) {
    g_log->log_error(std::format("invalid cs_create_file data_len {}", (uint32_t)req_frame->data_len));
    co_return;
  }

  /* create file */
  auto file_id = hot_stores->create_file(*(uint64_t *)req_frame->data);
  if (!file_id) {
    g_log->log_error("failed to create file");
    co_await conn->send_res_frame(proto_frame_t{.stat = 2}, req_frame);
    co_return;
  }
  conn->set_data((uint64_t)conn_data_e::c_upload_file_id, file_id.value());

  /* response */
  co_await conn->send_res_frame(proto_frame_t{}, req_frame);
  co_return;
}

auto cs_upload_append_handle(REQ_HANDLE_PARAMS) -> asio::awaitable<void> {
  auto file_id = conn->get_data<uint64_t>((uint64_t)conn_data_e::c_upload_file_id);
  if (!file_id) {
    g_log->log_warn(std::format("client {} not create file yield", conn->to_string()));
    co_await conn->send_res_frame(proto_frame_t{.stat = 1}, req_frame);
    co_return;
  }

  /* append data */
  auto data = std::span<uint8_t>{(uint8_t *)req_frame->data, req_frame->data_len};
  if (!hot_stores->write_file(file_id.value(), data)) {
    g_log->log_error("failed to write file");
    co_await conn->send_res_frame(proto_frame_t{.stat = 2}, req_frame);
    co_return;
  }

  /* response */
  co_await conn->send_res_frame(proto_frame_t{.cmd = req_frame->cmd}, req_frame);
}

auto cs_close_file_handle(REQ_HANDLE_PARAMS) -> asio::awaitable<void> {
  auto file_id = conn->get_data<uint64_t>((uint64_t)conn_data_e::c_upload_file_id);
  if (!file_id) {
    g_log->log_warn(std::format("client {} not create file yield", conn->to_string()));
    co_await conn->send_res_frame(proto_frame_t{.stat = 1}, req_frame);
    co_return;
  }

  /* close file */
  auto file_name = std::string{(char *)req_frame->data, req_frame->data_len};
  auto new_file_path = hot_stores->close_file(file_id.value(), file_name);
  if (!new_file_path) {
    g_log->log_error("failed to close file");
    co_await conn->send_res_frame(proto_frame_t{.stat = 2}, req_frame);
    co_return;
  }

  /* 加入到未同步队列 */
  {
    auto lock = std::unique_lock{unsync_uploaded_files_mut};
    unsync_uploaded_files.emplace(new_file_path.value());
  }

  /* response */
  auto res_path = std::format("{}/{}", storage_group_id, new_file_path.value());
  auto res_frame = (proto_frame_t *)malloc(sizeof(proto_frame_t) + res_path.size());
  *res_frame = {.data_len = (uint32_t)res_path.size()};
  std::memcpy(res_frame->data, res_path.data(), res_path.size());
  co_await conn->send_res_frame(std::shared_ptr<proto_frame_t>{res_frame, [](auto p) { free(p); }}, req_frame);
  conn->del_data((uint64_t)conn_data_e::c_upload_file_id);
  co_return;
}

auto cs_open_file_handle(REQ_HANDLE_PARAMS) -> asio::awaitable<void> {
  if (conn->has_data((uint64_t)conn_data_e::c_open_file_id)) {
    g_log->log_error(std::format("file already opened {}", conn->to_string()));
    co_await conn->send_res_frame(proto_frame_t{.stat = 1}, req_frame);
    co_return;
  }

  /* check group id valid */
  auto file_path = std::string{(char *)req_frame->data, req_frame->data_len};
  auto group_id = std::atol(file_path.substr(0, file_path.find_first_of('/')).data());
  if (group_id != storage_group_id) {
    g_log->log_error(std::format("invalid group id {}", group_id));
    co_await conn->send_res_frame(proto_frame_t{.stat = 2}, req_frame);
    co_return;
  }
  file_path = file_path.substr(file_path.find_first_of('/') + 1);

  /* search file  */
  for (auto store : stores) {
    auto _ = store->open_file(file_path);
    if (!_.has_value()) {
      continue;
    }

    /* response suc */
    auto [file_id, file_size] = _.value();
    conn->set_data((uint64_t)conn_data_e::c_open_file_id, file_id);
    auto res_frame = (proto_frame_t *)malloc(sizeof(proto_frame_t) + sizeof(uint64_t));
    *res_frame = {.data_len = sizeof(uint64_t)};
    *((uint64_t *)res_frame->data) = file_size;
    co_await conn->send_res_frame(std::shared_ptr<proto_frame_t>{res_frame, [](auto p) { free(p); }}, req_frame);
    co_return;
  }

  /* not find file */
  co_await conn->send_res_frame(proto_frame_t{.stat = 3}, req_frame);
}

auto cs_download_file_handle(REQ_HANDLE_PARAMS) -> asio::awaitable<void> {
  if (!conn->has_data((uint64_t)conn_data_e::c_open_file_id)) {
    g_log->log_error(std::format("file not opened {}", conn->to_string()));
    co_await conn->send_res_frame(proto_frame_t{.stat = 1}, req_frame);
    co_return;
  }

  auto file_id = conn->get_data<uint64_t>((uint64_t)conn_data_e::c_open_file_id).value();
  auto file_data = hot_stores->read_file(file_id, *(uint32_t *)req_frame->data);
  if (!file_data.has_value()) {
    g_log->log_error("failed to read file");
    co_await conn->send_res_frame(proto_frame_t{.stat = 2}, req_frame);
    co_return;
  }

  auto res_frame = (proto_frame_t *)malloc(sizeof(proto_frame_t) + file_data->size());
  *res_frame = {.data_len = (uint32_t)file_data->size()};
  std::copy(file_data->begin(), file_data->end(), res_frame->data);
  co_await conn->send_res_frame(std::shared_ptr<proto_frame_t>{res_frame, [](auto p) { free(p); }}, req_frame);
  co_return;
}

std::map<proto_cmd_e, req_handle_t> client_req_handles{
    {(proto_cmd_e)proto_cmd_e::cs_upload_create, cs_upload_create_handle},
    {(proto_cmd_e)proto_cmd_e::cs_upload_append, cs_upload_append_handle},
    {(proto_cmd_e)proto_cmd_e::cs_upload_close, cs_close_file_handle},
    {(proto_cmd_e)proto_cmd_e::cs_open_file, cs_open_file_handle},
    {(proto_cmd_e)proto_cmd_e::cs_download_file, cs_download_file_handle},
};

auto recv_from_client(std::shared_ptr<connection_t> conn) -> asio::awaitable<void> {
  while (true) {
    auto req_frame = co_await conn->recv_req_frame();
    if (!req_frame) {
      co_return co_await on_client_disconnect(conn);
    }

    /* storage 注册后视为 storage 处理 */
    if (req_frame->cmd == (uint8_t)proto_cmd_e::ss_regist) {
      co_return co_await ss_regist_handle(conn, req_frame);
    }

    auto handle = client_req_handles.find((proto_cmd_e)req_frame->cmd);
    if (handle == client_req_handles.end()) {
      g_log->log_warn(std::format("unhandle cmd {} from client {}", req_frame->cmd, conn->to_string()));
      continue;
    }
    co_await handle->second(conn, req_frame);
  }
}
