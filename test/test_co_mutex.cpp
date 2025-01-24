#include <asio.hpp>
#include <print>

auto bench_1() -> void {
    auto begin = std::chrono::high_resolution_clock::now();

    int n = 0;
    auto io = asio::io_context{};
    auto s = asio::make_strand(io);

    for (auto i = 0; i < 20; ++i) {
        asio::co_spawn(io, [&io, &n, &s] -> asio::awaitable<void> {
            for(auto i=0;i<10000;++i){
                co_await asio::post(s, asio::use_awaitable);
                ++n;
            } }(), asio::detached);
    }

    for (auto i = 0; i < 5; ++i) {
        std::thread{[&io] {
            io.run();
        }}.detach();
    }

    io.run();

    auto end = std::chrono::high_resolution_clock::now();
    std::println("n = {}, time: {}", n, std::chrono::duration<double>(end - begin).count());
}

auto bench_2() -> void {
    auto begin = std::chrono::high_resolution_clock::now();

    int n = 0;
    auto mu = std::mutex{};
    auto io = asio::io_context{};
    for (auto i = 0; i < 20; ++i) {
        asio::co_spawn(io, [&] -> asio::awaitable<void> {
            for(auto i=0;i<10000;++i){
                mu.lock();
                ++n;
                mu.unlock();
            } 
            co_return; }(), asio::detached);
    }

    io.run();
    auto end = std::chrono::high_resolution_clock::now();
    std::println("n = {}, time: {}", n, std::chrono::duration<double>(end - begin).count());
}

auto main() -> int {
    bench_1();
    bench_2();

    return 0;
}