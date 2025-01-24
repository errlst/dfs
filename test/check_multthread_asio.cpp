/* 验证 asio::io_context 多线程读 */
#include <asio.hpp>
#include <iostream>

auto accept_io = asio::io_context{};
auto read_io = asio::io_context{};
auto work_guard = asio::make_work_guard(read_io);

auto do_read(asio::ip::tcp::socket sock) -> asio::awaitable<void> {
    char buf[1024];
    while (true) {
        auto [ec, n] = co_await sock.async_read_some(asio::buffer(buf), asio::as_tuple(asio::use_awaitable));
        if (ec) {
            std::cout << "read error: " << ec.message() << "\n";
            break;
        }
        buf[n] = 0;
        std::cout << buf << "\n";
    }
}

auto do_accept() -> asio::awaitable<void> {
    auto acceptor = asio::ip::tcp::acceptor{accept_io, asio::ip::tcp::endpoint{asio::ip::tcp::v4(), 8888}};
    std::cout << "start accept\n";
    for (auto i = 0; i < 2; ++i) {
        std::thread{[] {
            while (true) {
                read_io.run();
            }
        }}.detach();
    }
    while (true) {
        auto [ec, sock] = co_await acceptor.async_accept(asio::as_tuple(asio::use_awaitable));
        if (ec) {
            std::cout << "accept error: " << ec.message() << "\n";
            continue;
        }
        std::cout << "new connection\n";
        asio::co_spawn(read_io, do_read(std::move(sock)), asio::detached);
    }
}

auto main() -> int {
    asio::co_spawn(accept_io, do_accept(), asio::detached);
    accept_io.run();

    return 0;
}
