#pragma once

#include "asio.hpp"
#include <string_view>

auto check_directory(std::string_view path) -> void;

auto co_sleep_for(std::chrono::milliseconds ms) -> asio::awaitable<void>;