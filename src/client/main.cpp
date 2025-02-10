#include "../common/connection.h"
#include "../common/protocol.h"
#include "../common/util.h"
#include "../proto/proto.pb.h"
#include <fstream>

auto master_conn = std::shared_ptr<common::connection>{};

auto upload_file(std::string_view path) -> asio::awaitable<void> {
  if (!std::filesystem::exists(path)) {
    LOG_ERROR(std::format("invalid file ", path));
  }

  /* request */
  auto request_to_send = (common::proto_frame *)malloc(sizeof(common::proto_frame) + sizeof(uint64_t));
  *request_to_send = {
      .cmd = common::proto_cmd::cm_fetch_one_storage,
      .data_len = sizeof(uint64_t),
  };
  *((uint64_t *)request_to_send->data) = htonll(std::filesystem::file_size(path));
  auto id = co_await master_conn->send_request(request_to_send);
  free(request_to_send);
  if (!id) {
    LOG_ERROR("failed to send cm_fetch_one_storage request");
    co_return;
  }

  auto response_recved = co_await master_conn->recv_response(id.value());
  if (!response_recved) {
    LOG_ERROR("failed to recv cm_fetch_one_storage response");
    co_return;
  }

  auto response_data_recved = proto::cm_fetch_one_storage_response{};
  if (!response_data_recved.ParseFromArray(response_recved->data, response_recved->data_len)) {
    LOG_ERROR("failed to parse cm_fetch_one_storage_response");
    co_return;
  }

  /* connect to storage */
  auto conn = co_await common::connection::connect_to(response_data_recved.s_info().ip(), response_data_recved.s_info().port());
  if (!conn) {
    LOG_ERROR(std::format("failed to connect to storage {}:{}", response_data_recved.s_info().ip(), response_data_recved.s_info().port()));
    co_return;
  }
  LOG_INFO(std::format("connect to storage {}:{} suc", response_data_recved.s_info().ip(), response_data_recved.s_info().port()));
  conn->start([](std::shared_ptr<common::proto_frame>, std::shared_ptr<common::connection>) -> asio::awaitable<void> {
    co_return;
  });

  /* 开始上传 */
  LOG_INFO(std::format("start upload file"));
  request_to_send = (common::proto_frame *)malloc(sizeof(common::proto_frame) + sizeof(uint64_t));
  *request_to_send = {
      .cmd = common::proto_cmd::cs_upload_open,
      .data_len = sizeof(uint64_t),
  };
  *((uint64_t *)request_to_send->data) = htonll(std::filesystem::file_size(path));
  id = co_await conn->send_request(request_to_send);
  free(request_to_send);
  if (!id) {
    LOG_ERROR("failed to send cs_upload_open request");
    co_return;
  }

  response_recved = co_await conn->recv_response(id.value());
  if (!response_recved) {
    LOG_ERROR("failed to recv cs_upload_open response");
    co_return;
  }
  if (response_recved->stat != 0) {
    LOG_ERROR(std::format("cs_upload_open response stat {}", response_recved->stat));
    co_return;
  }

  /* 上传数据 */
  bool ok = true;
  request_to_send = (common::proto_frame *)malloc(sizeof(common::proto_frame) + 1024 * 1024 * 5);
  auto idx = 0;
  auto ifs = std::ifstream{std::string{path}, std::ios::binary};
  if (!ifs) {
    LOG_ERROR(std::format("failed open file {}", strerror(errno)));
    co_return;
  }
  while (!ifs.eof()) {
    LOG_INFO(std::format("upload file trunk {}", idx++));
    *request_to_send = common::proto_frame{
        .cmd = common::proto_cmd::cs_upload_append,
        .data_len = (uint32_t)ifs.readsome(request_to_send->data, 1024 * 1024 * 5),
    };
    if (request_to_send->data_len == 0) {
      LOG_ERROR(std::format("read file failed {}", strerror(errno)));
      break;
    }
    id = co_await conn->send_request(request_to_send);
    if (!id) {
      LOG_ERROR("failed to send cs_upload_append request");
      ok = false;
      break;
    }

    response_recved = co_await conn->recv_response(id.value());
    if (!response_recved) {
      LOG_ERROR("failed to recv cs_upload_append response");
      ok = false;
      break;
    }
    if (response_recved->stat != 0) {
      LOG_ERROR(std::format("cs_upload_append response stat {}", response_recved->stat));
      ok = false;
      break;
    }
  }
  free(request_to_send);
  if (!ok) {
    co_return;
  }

  /* 结束上传 */
  LOG_INFO(std::format("close upload"));
  auto file_name = path.substr(path.find_last_of('/') + 1);
  request_to_send = (common::proto_frame *)malloc(sizeof(common::proto_frame) + file_name.size());
  *request_to_send = {
      .cmd = common::proto_cmd::cs_upload_close,
      .data_len = (uint32_t)file_name.size(),
  };
  std::copy(file_name.begin(), file_name.end(), request_to_send->data);

  id = co_await conn->send_request(request_to_send);
  if (!id) {
    LOG_ERROR("failed to send cs_upload_close request");
    co_return;
  }

  response_recved = co_await conn->recv_response(id.value());
  if (!response_recved) {
    LOG_ERROR("failed to recv cs_upload_close response");
    co_return;
  }
  if (response_recved->stat != 0) {
    LOG_ERROR(std::format("cs_upload_close response stat {}", response_recved->stat));
    co_return;
  }

  auto file_path = std::string_view{response_recved->data, response_recved->data_len};
  LOG_INFO(std::format("upload suc file: {}", file_path));
}

// auto download_file(std::string_view src, std::string_view dst) -> asio::awaitable<void> {
//   auto ofs = std::ofstream{std::string{dst}, std::ios::binary};
//   if (!ofs) {
//     g_m_log->log_error(std::format("failed to open file {}", dst));
//     co_return;
//   }

//   /* request storages */
//   auto req_frame = (proto_frame_t *)malloc(sizeof(proto_frame_t) + sizeof(uint32_t));
//   *req_frame = {
//       .cmd = (uint8_t)proto_cmd_e::cm_group_storages,
//       .data_len = sizeof(uint32_t),
//   };
//   *((uint32_t *)req_frame->data) = std::atol(src.substr(0, src.find_first_of('/')).data());
//   auto id = co_await master_conn->send_req_frame(std::shared_ptr<proto_frame_t>{req_frame, [](auto p) { free(p); }});
//   if (!id) {
//     g_m_log->log_error("failed to send cm_group_storages request");
//     co_return;
//   }

//   /*  */
//   auto res_frame = co_await master_conn->recv_res_frame(id.value());
//   if (!res_frame) {
//     g_m_log->log_error("failed to recv cm_group_storages response");
//     co_return;
//   }
//   if (res_frame->stat) {
//     g_m_log->log_error(std::format("cm_group_storages response stat {}", res_frame->stat));
//     co_return;
//   }
//   auto res_data = dfs::proto::cm_group_storages::response_t{};
//   if (!res_data.ParseFromArray(res_frame->data, res_frame->data_len)) {
//     g_m_log->log_error("failed to parse cm_group_storages response");
//     co_return;
//   }

//   /* 遍历 storage 获取有效的 connection */
//   auto storage_conn = std::shared_ptr<connection>{};
//   for (const auto &s_info : res_data.storages()) {
//     storage_conn = co_await connection_t::connect_to(*io,s_info.ip(), s_info.port(), g_log);
//     if (!storage_conn) {
//       g_log->log_error(std::format("failed to connect to storage {}:{}", s_info.ip(), s_info.port()));
//       continue;
//     }

//     /* request open file */
//     req_frame = (proto_frame_t *)malloc(sizeof(proto_frame_t) + src.size());
//     *req_frame = {
//         .cmd = (uint8_t)proto_cmd_e::cs_open_file,
//         .data_len = (uint32_t)src.size(),
//     };
//     std::copy(src.begin(), src.end(), req_frame->data);
//     id = co_await storage_conn->send_req_frame(std::shared_ptr<proto_frame_t>{req_frame, [](auto p) { free(p); }});
//     if (!id) {
//       g_log->log_error("failed to send cs_open_file request");
//       continue;
//     }

//     /* wait response */
//     res_frame = co_await storage_conn->recv_res_frame(id.value());
//     if (!res_frame) {
//       g_log->log_error("failed to recv cs_open_file response");
//       storage_conn = nullptr;
//       continue;
//     }
//     if (res_frame->stat) {
//       g_log->log_error(std::format("cs_open_file response stat {}", res_frame->stat));
//       storage_conn = nullptr;
//       continue;
//     }
//     break;
//   }

//   if (!storage_conn) {
//     g_m_log->log_warn("no valid storage");
//     co_return;
//   }

//   g_m_log->log_info(std::format("watting download from storage {} file_size is {}", storage_conn->to_string(), *((uint64_t *)res_frame->data)));

//   while (true) {
//     /* 每次下载 5MB */
//     req_frame = (proto_frame_t *)malloc(sizeof(proto_frame_t) + sizeof(uint32_t));
//     *req_frame = {
//         .cmd = (uint8_t)proto_cmd_e::cs_download_file,
//         .data_len = sizeof(uint32_t),
//     };
//     *((uint32_t *)req_frame->data) = 5 * 1024 * 1024;
//     id = co_await storage_conn->send_req_frame(std::shared_ptr<proto_frame_t>{req_frame, [](auto p) { free(p); }});
//     if (!id) {
//       g_m_log->log_error("failed to send cs_download_file request");
//       break;
//     }

//     res_frame = co_await storage_conn->recv_res_frame(id.value());
//     if (!res_frame) {
//       g_m_log->log_error("failed to recv cs_download_file response");
//       break;
//     }
//     if (res_frame->stat) {
//       g_m_log->log_error(std::format("cs_download_file response stat {}", res_frame->stat));
//       break;
//     }
//     if (res_frame->data_len == 0) {
//       g_m_log->log_info("download suc");
//       ofs.flush();
//       break;
//     }

//     ofs.write(res_frame->data, res_frame->data_len);
//     g_m_log->log_info(std::format("download {} bytes", (uint32_t)res_frame->data_len));
//   }
// }

auto client() -> asio::awaitable<void> {
  master_conn = co_await common::connection::connect_to("127.0.0.1", 8888);
  if (!master_conn) {
    LOG_ERROR(std::format("failed connect to master"));
    co_return;
  }
  master_conn->start([](std::shared_ptr<common::proto_frame>, std::shared_ptr<common::connection>) -> asio::awaitable<void> {
    co_return;
  });
  LOG_INFO("connect to master suc");
}

auto main() -> int {
  auto io = asio::io_context{};

  asio::co_spawn(io, client(), asio::detached);
  std::thread{[&] {
    auto guard = asio::make_work_guard(io);
    io.run();
  }}.detach();

  auto cmd = std::string{};
  while (true) {
    std::this_thread::sleep_for(std::chrono::seconds(2));
    asio::co_spawn(io, upload_file("bigfile"), asio::detached);
    // std::cout << ": " << std::flush;
    // std::cin >> cmd;
    // if (cmd == "upload") {
    //   std::string path;
    //   std::cin >> path;
    //   asio::co_spawn(io, upload_file(path), asio::detached);
    // } else if (cmd == "download") {
    //   std::string src, dst;
    //   std::cin >> src >> dst;
    //   // asio::co_spawn(*io, download_file(src, dst), asio::detached);
    // }
  }

  return 0;
}