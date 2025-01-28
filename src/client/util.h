#pragma once
#include "../common/connection.h"

auto connect_to_master(std::string_view ip, uint16_t port, std::shared_ptr<log_t> log) -> asio::awaitable<std::shared_ptr<connection_t>>;