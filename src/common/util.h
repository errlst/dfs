#pragma once

#include "asio.hpp"
#include <string_view>

auto check_directory(std::string_view path) -> void;

auto co_sleep_for(std::chrono::milliseconds ms) -> asio::awaitable<void>;

/* 文件系统可用空间 <free, total> */
auto fs_free_size(std::string_view path) -> std::tuple<uint64_t, uint64_t>;