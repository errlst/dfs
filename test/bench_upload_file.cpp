/**
 * @file bench_upload_small_file.cpp
 * @author your name (you@domain.com)
 * @brief 测试上传小文件的性能
 * @version 0.1
 * @date 2025-03-02
 *
 * @copyright Copyright (c) 2025
 *
 */

#include "../src/common/connection.h"
#include "../src/common/protocol.h"
#include "../src/common/util.h"
#include "../src/proto/proto.pb.h"
#include "common/log.h"
#include <print>

auto show_usage() {
  std::println("Usage: bench_upload_file <fork_times> <times> <file_size>");
}

auto io = asio::io_context{};
auto master_conn = std::shared_ptr<common::connection>{};
auto file_to_write = std::string{};

auto upload_file(uint64_t file_size) -> asio::awaitable<void> {
  auto request_to_send = std::shared_ptr<common::proto_frame>{(common::proto_frame *)malloc(sizeof(common::proto_frame) + sizeof(uint64_t)), free};
  *request_to_send = {
      .cmd = common::proto_cmd::cm_fetch_one_storage,
      .data_len = sizeof(uint64_t),
  };
  *((uint64_t *)request_to_send->data) = common::htonll(file_size);
  auto id = co_await master_conn->send_request(request_to_send.get());
  auto response = co_await master_conn->recv_response(id.value());
  if (response->stat != 0) {
    std::println("Failed to fetch storage");
    co_return;
  }

  auto storage = proto::cm_fetch_one_storage_response{};
  if (!storage.ParseFromArray(response->data, response->data_len)) {
    std::println("Failed to parse response");
    co_return;
  }

  auto storage_conn = co_await common::connection::connect_to(storage.s_info().ip(), storage.s_info().port());
  storage_conn->start([](std::shared_ptr<common::proto_frame>, std::shared_ptr<common::connection>) -> asio::awaitable<void> {
    co_return;
  });

  request_to_send = std::shared_ptr<common::proto_frame>{(common::proto_frame *)malloc(sizeof(common::proto_frame) + sizeof(uint64_t)), free};
  *request_to_send = {
      .cmd = common::proto_cmd::cs_upload_open,
      .data_len = sizeof(uint64_t),
  };
  *((uint64_t *)request_to_send->data) = common::htonll(file_size);
  id = co_await storage_conn->send_request(request_to_send.get());
  response = co_await storage_conn->recv_response(id.value());
  if (response->stat != 0) {
    std::println("upload failed {} {}", __LINE__, response->stat);
    co_return;
  }

  auto idx = 0uz;
  request_to_send = common::create_request_frame(common::proto_cmd::cs_upload_append, 1_MB);
  while (idx < file_size) {
    auto end_idx = std::min(idx + 1_MB, file_size);
    request_to_send->data_len = (uint32_t)(end_idx - idx);
    LOG_INFO("end_idx {}, idx {}, file_size {}", end_idx, idx, file_size);
    std::memcpy(request_to_send->data, file_to_write.data() + idx, end_idx - idx);

    response = co_await storage_conn->send_request_and_wait_response(request_to_send.get());
    if (!response || response->stat != 0) {
      LOG_ERROR("upload failed {}", response ? response->stat : -1);
      co_return;
    }
    idx = end_idx;
  }

  response = co_await storage_conn->send_request_and_wait_response(common::proto_frame{.cmd = common::proto_cmd::cs_upload_close});
  if (!response || response->stat != 0) {
    LOG_ERROR("upload failed {}", response ? response->stat : -1);
    co_return;
  }
  co_await storage_conn->close();
  co_return;
}

auto run(uint64_t times, uint64_t file_size) -> asio::awaitable<void> {
  master_conn = co_await common::connection::connect_to("127.0.0.1", 8888);
  master_conn->start([](std::shared_ptr<common::proto_frame>, std::shared_ptr<common::connection>) -> asio::awaitable<void> {
    co_return;
  });

  for (auto i = 0uz; i < times; ++i) {
    co_await upload_file(file_size);
  }
  co_await master_conn->close();
}

auto main(int argc, char *argv[]) -> int {
  if (argc != 4) {
    show_usage();
    return -1;
  }

  spdlog::set_level(spdlog::level::debug);
  spdlog::set_pattern("[%Y-%m-%d %H:%M:%S.%e] %^[%l] [%s:%#]%$ %v");

  auto fork_times = std::stoll(argv[1]) - 1;
  auto times = std::stoll(argv[2]);
  auto file_size = std::stoll(argv[3]);

  file_to_write = common::random_string(file_size);

  for (auto i = 0; i < fork_times; ++i) {
    if (0 == fork()) {
      break;
    }
  }

  asio::co_spawn(io, run(times, file_size), asio::detached);
  io.run();
  return 0;
}