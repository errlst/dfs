#include "../src/common/connection.h"
#include "../src/common/protocol.h"

auto upload_file() -> asio::awaitable<void> {
  auto master_conn = co_await common::connection::connect_to("127.0.0.1", 8888);
  master_conn->start([](std::shared_ptr<common::proto_frame>, std::shared_ptr<common::connection>) -> asio::awaitable<void> {
    co_return;
  });

  auto request_to_send = (common::proto_frame *)malloc(sizeof(common::proto_frame) + sizeof(uint64_t));
  while (true) {
    *request_to_send = {
        .cmd = common::proto_cmd::cm_fetch_one_storage,
        .data_len = sizeof(uint64_t),
    };
    *((uint64_t *)request_to_send->data) = 0;
    co_await master_conn->send_request(request_to_send);
  }
}

auto main() -> int {
  auto io = asio::io_context{};
  for (auto i = 0; i < 20; ++i) {
    std::thread{[&io] {
      auto gurad = asio::make_work_guard(io);
      asio::co_spawn(io, upload_file(), asio::detached);
      io.run();
    }}.detach();
  }

  auto guard = asio::make_work_guard(io);
  return io.run();
}