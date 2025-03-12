#include "../common/connection.h"
#include "../common/protocol.h"
#include "../common/util.h"
#include "../proto/proto.pb.h"
#include <fstream>

auto master_conn = std::shared_ptr<common::connection>{};

auto upload_file(std::string path) -> asio::awaitable<void> {
  if (!std::filesystem::exists(path)) {
    LOG_ERROR(std::format("invalid file ", path));
  }

  /* request */
  auto request_to_send = std::shared_ptr<common::proto_frame>{(common::proto_frame *)malloc(sizeof(common::proto_frame) + sizeof(uint64_t)), free};
  *request_to_send = {
      .cmd = common::proto_cmd::cm_fetch_one_storage,
      .data_len = sizeof(uint64_t),
  };
  *((uint64_t *)request_to_send->data) = common::htonll(std::filesystem::file_size(path));
  auto id = co_await master_conn->send_request(request_to_send.get());
  if (!id) {
    LOG_ERROR("failed to send cm_fetch_one_storage request");
    co_return;
  }

  auto response_recved = co_await master_conn->recv_response(id.value());
  if (!response_recved) {
    LOG_ERROR("failed to recv cm_fetch_one_storage response");
    co_return;
  }
  if (response_recved->stat != 0) {
    LOG_ERROR(std::format("cm_fetch_one_storage response stat {}", response_recved->stat));
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
  request_to_send = std::shared_ptr<common::proto_frame>{(common::proto_frame *)malloc(sizeof(common::proto_frame) + sizeof(uint64_t)), free};
  *request_to_send = {
      .cmd = common::proto_cmd::cs_upload_open,
      .data_len = sizeof(uint64_t),
  };
  *((uint64_t *)request_to_send->data) = common::htonll(std::filesystem::file_size(path));
  id = co_await conn->send_request(request_to_send.get());
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
  request_to_send = std::shared_ptr<common::proto_frame>{(common::proto_frame *)malloc(sizeof(common::proto_frame) + 5_MB), free};
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
        .data_len = (uint32_t)ifs.readsome(request_to_send->data, 5_MB),
    };
    if (request_to_send->data_len == 0) {
      break;
    }

    id = co_await conn->send_request(request_to_send.get());
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
  if (!ok) {
    co_return;
  }

  /* 结束上传 */
  LOG_INFO(std::format("close upload"));
  auto file_name = path.substr(path.find_last_of('/') + 1);
  request_to_send = std::shared_ptr<common::proto_frame>{(common::proto_frame *)malloc(sizeof(common::proto_frame) + file_name.size()), free};
  *request_to_send = {
      .cmd = common::proto_cmd::cs_upload_close,
      .data_len = (uint32_t)file_name.size(),
  };
  std::copy(file_name.begin(), file_name.end(), request_to_send->data);

  id = co_await conn->send_request(request_to_send.get());
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

  co_await conn->close();
}

auto download_file(std::string src, std::string dst) -> asio::awaitable<void> {
  auto ofs = std::ofstream{std::string{dst}, std::ios::binary};
  if (!ofs) {
    LOG_ERROR(std::format("failed to open file {}", dst));
    co_return;
  }

  auto group_id = std::atol(src.substr(0, src.find_first_of('/')).data());

  /* request storages */
  auto request_to_send = std::shared_ptr<common::proto_frame>{(common::proto_frame *)malloc(sizeof(common::proto_frame) + sizeof(uint32_t)), [](auto p) { free(p); }};
  *request_to_send = {
      .cmd = common::proto_cmd::cm_fetch_group_storages,
      .data_len = sizeof(uint32_t),
  };
  *((uint32_t *)request_to_send->data) = htonl(group_id);
  auto id = co_await master_conn->send_request(request_to_send.get());
  if (!id) {
    LOG_ERROR("failed to send cm_group_storages request");
    co_return;
  }

  auto response_recved = co_await master_conn->recv_response(id.value());
  if (!response_recved) {
    LOG_ERROR("failed to recv cm_group_storages response");
    co_return;
  }
  if (response_recved->stat) {
    LOG_ERROR(std::format("cm_group_storages response stat {}", response_recved->stat));
    co_return;
  }
  if (response_recved->data_len == 0) {
    LOG_ERROR(std::format("no valid storage in group {}", group_id));
    co_return;
  }

  auto response_data_recved = proto::cm_fetch_group_storages_response{};
  if (!response_data_recved.ParseFromArray(response_recved->data, response_recved->data_len)) {
    LOG_ERROR("failed to parse cm_group_storages response");
    co_return;
  }

  /* connect to storage */
  src = src.substr(src.find_first_of('/') + 1);
  LOG_INFO(std::format("request download {}", src));
  for (auto s_info : response_data_recved.s_infos()) {
    auto conn = co_await common::connection::connect_to(s_info.ip(), s_info.port());
    if (!conn) {
      LOG_ERROR(std::format("failed to connect to storage {}:{}", s_info.ip(), s_info.port()));
      continue;
    }
    LOG_INFO(std::format("connect to storage {}:{} suc", s_info.ip(), s_info.port()));
    conn->start([](std::shared_ptr<common::proto_frame>, std::shared_ptr<common::connection>) -> asio::awaitable<void> {
      co_return;
    });

    /* download open */
    request_to_send = std::shared_ptr<common::proto_frame>{(common::proto_frame *)malloc(sizeof(common::proto_frame) + src.size()), [](auto p) { free(p); }};
    *request_to_send = {
        .cmd = common::proto_cmd::cs_download_open,
        .data_len = (uint32_t)src.size(),
    };
    std::copy(src.begin(), src.end(), request_to_send->data);
    id = co_await conn->send_request(request_to_send.get());
    if (!id) {
      LOG_ERROR("failed to send cs_download_open request");
      continue;
    }

    response_recved = co_await conn->recv_response(id.value());
    if (!response_recved) {
      LOG_ERROR("failed to recv cs_download_open response");
      continue;
    }
    if (response_recved->stat) {
      LOG_ERROR(std::format("cs_download_open response stat {}", response_recved->stat));
      continue;
    }

    auto ifs = std::ifstream{std::string{dst}, std::ios::binary};
    if (!ifs) {
      LOG_ERROR(std::format("failed to open file {}", dst));
      co_return;
    }

    /* download data */
    LOG_INFO(std::format("start download filesize {}", common::ntohll(*(uint64_t *)response_recved->data)));
    while (true) {
      auto id = co_await conn->send_request(common::proto_frame{.cmd = common::proto_cmd::cs_download_append});
      if (!id) {
        LOG_ERROR("failed to send cs_download_append request");
        break;
      }

      response_recved = co_await conn->recv_response(id.value());
      if (!response_recved) {
        LOG_ERROR("failed to recv cs_download_append response");
        break;
      }
      if (response_recved->stat) {
        LOG_ERROR(std::format("cs_download_append response stat {}", response_recved->stat));
        break;
      }

      if (response_recved->data_len == 0) {
        LOG_INFO(std::format("download {} suc", src));
        co_return;
      }

      ofs.write(response_recved->data, response_recved->data_len);
    }
  }
}

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

  for (auto i = 0; i < 10; ++i) {
    std::thread{[&io] {
      auto gurad = asio::make_work_guard(io);
      io.run();
    }}.detach();
  }

  auto cmd = std::string{};
  while (true) {
    std::cout << ": " << std::flush;
    std::cin >> cmd;
    if (cmd == "upload") {
      std::string path;
      std::cin >> path;
      asio::co_spawn(io, upload_file(path), asio::detached);
    } else if (cmd == "download") {
      std::string src, dst;
      std::cin >> src >> dst;
      asio::co_spawn(io, download_file(src, dst), asio::detached);
    }
  }

  return 0;
}