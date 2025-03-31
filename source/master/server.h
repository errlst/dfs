#pragma once

#include <common/connection.h>
#include <common/protocol.h>

namespace master {

  auto request_from_connection(common::proto_frame_ptr request, common::connection_ptr conn) -> asio::awaitable<void>;

  auto master_server() -> asio::awaitable<void>;

} // namespace master