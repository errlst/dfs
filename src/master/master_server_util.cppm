module;

#include <asio.hpp>
#include <cstdint>
#include <functional>
#include <string>

export module master.server_util;

import common;

export namespace master {

  enum conn_data : uint64_t {
    conn_type,
    storage_id,
    storage_ip,
    storage_port,
    storage_magic,
    storage_max_free_space,
    storage_max_free_space_timer,
  };

  enum class conn_type_t {
    client,
    storage,
  };

  using storage_id_t = uint32_t;
  using storage_ip_t = std::string;
  using storage_port_t = uint16_t;
  using storage_magic_t = uint32_t;
  using storage_max_free_space_t = uint64_t;
  using storage_max_free_space_timer_t = std::shared_ptr<asio::steady_timer>;

  using request_handle_t = std::function<asio::awaitable<bool>(common::proto_frame_ptr, common::connection_ptr)>;

} // namespace master