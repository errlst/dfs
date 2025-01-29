#include "util.h"
#include <filesystem>
#include <iostream>
#include <print>
#include <random>
#include <sys/statvfs.h>

auto check_directory(std::string_view path) -> void {
    if (std::filesystem::exists(path)) {
        if (!std::filesystem::is_directory(path)) {
            std::cerr << path << " is not a directory\n";
            exit(-1);
        }
    } else {
        if (!std::filesystem::create_directories(path)) {
            std::cerr << "failed to create directory: " << path << "\n";
            exit(-1);
        }
    }
}

auto co_sleep_for(std::chrono::milliseconds ms) -> asio::awaitable<void> {
    auto timer = asio::steady_timer{co_await asio::this_coro::executor, ms};
    co_await timer.async_wait(asio::use_awaitable);
}

auto fs_free_size(std::string_view path) -> std::tuple<uint64_t, uint64_t> {
    struct statvfs stat;
    if (statvfs(path.data(), &stat) != 0) {
        return std::make_tuple(0ull, 0ull);
    }
    return std::make_tuple(stat.f_bavail * stat.f_bsize, stat.f_blocks * stat.f_bsize);
}

auto random_string(uint32_t len) -> std::string {
    constexpr auto chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    static auto rng = std::default_random_engine(std::random_device()());
    auto res = std::string(len, 0);
    for (auto i = 0; i < len; ++i) {
        res[i] = chars[rng() % 62];
    }
    return res;
}
