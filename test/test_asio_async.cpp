/**
 * @file 测试单个线程中，多个协程对同一个 socket 进行连续写入操作，会导致不同协程的数据混乱
 *
 */

#include <asio.hpp>
#include <spdlog/spdlog.h>

char client_work_1_buf[10240000];
char client_work_2_buf[10240000];
char server_buf[10240000];

uint32_t client_len;

auto client_work_1(asio::ip::tcp::socket &sock) -> asio::awaitable<void> {
  while (true) {
    auto len = ++client_len;
    memcpy(client_work_1_buf, &len, sizeof(len));
    auto [ec, n] = co_await asio::async_write(sock, asio::const_buffer(client_work_1_buf, len + sizeof(len)), asio::as_tuple(asio::use_awaitable));
    if (ec || n != len + sizeof(len)) {
      spdlog::error("work 1 write failed, {}", ec.message());
      co_return;
    }
    spdlog::debug("client work 1 send len {}", len);
  }
}

auto client_work_2(asio::ip::tcp::socket &sock) -> asio::awaitable<void> {
  while (true) {
    auto len = ++client_len;
    memcpy(client_work_2_buf, &len, sizeof(len));
    auto [ec, n] = co_await asio::async_write(sock, asio::const_buffer(client_work_2_buf, len + sizeof(len)), asio::as_tuple(asio::use_awaitable));
    if (ec || n != len + sizeof(len)) {
      spdlog::error("work 2 write failed, {}", ec.message());
      co_return;
    }
    spdlog::debug("client work 2 send len {}", len);
  }
}

auto client_thread() -> void {
  auto io = asio::io_context{};
  auto guard = asio::make_work_guard(io);
  std::this_thread::sleep_for(std::chrono::seconds{2});

  auto sock = asio::ip::tcp::socket{io};
  sock.connect(asio::ip::tcp::endpoint{asio::ip::make_address_v4("127.0.0.1"), 8888});
  if (!sock.is_open()) {
    spdlog::error("connect failed");
    return;
  }

  asio::co_spawn(io, client_work_1(sock), asio::detached);
  asio::co_spawn(io, client_work_2(sock), asio::detached);
  io.run();
}

auto server_work() -> asio::awaitable<void> {
  auto acceptor = asio::ip::tcp::acceptor{co_await asio::this_coro::executor, asio::ip::tcp::endpoint{asio::ip::tcp::v4(), 8888}};
  acceptor.listen();
  auto sock = asio::ip::tcp::socket{co_await asio::this_coro::executor};
  co_await acceptor.async_accept(sock, asio::use_awaitable);
  spdlog::info("client connected");

  while (true) {
    auto len = uint32_t{};
    auto [ec, n] = co_await asio::async_read(sock, asio::mutable_buffer(&len, sizeof(len)), asio::as_tuple(asio::use_awaitable));
    if (ec || n != sizeof(len)) {
      spdlog::error("server read failed, {}", ec.message());
      co_return;
    }

    spdlog::info("recv len {}", len);

    auto [ec2, n2] = co_await asio::async_read(sock, asio::mutable_buffer(server_buf, len), asio::as_tuple(asio::use_awaitable));
    if (ec2 || n2 != len) {
      spdlog::error("server read failed {}", ec2.message());
      co_return;
    }
  }
}

auto server_thread() -> void {
  auto io = asio::io_context{};
  auto guard = asio::make_work_guard(io);
  asio::co_spawn(io, server_work(), asio::detached);
  io.run();
}

auto main() -> int {
//   spdlog::set_level(spdlog::level::debug);

  std::thread{[] {
    server_thread();
  }}.detach();

  std::thread{[] {
    client_thread();
  }}.detach();

  while (true) {
    getchar();
  }

  return 0;
}