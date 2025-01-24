#pragma once
#include <asio.hpp>

/* work as master */
auto master_service() -> asio::awaitable<void>;
