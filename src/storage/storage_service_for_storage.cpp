#include "./storage_service_global.h"

auto on_storage_disconnect(std::shared_ptr<connection_t> conn) -> asio::awaitable<void> {
    g_log->log_warn(std::format("storage {} disconnect", conn->to_string()));
    storage_conns.erase(conn);
    co_return;
}

std::map<proto_cmd_e, req_handle_t> storage_req_handles{};

auto recv_from_storage(std::shared_ptr<connection_t> conn) -> asio::awaitable<void> {
    while (true) {
        auto req_frame = co_await conn->recv_req_frame();
        if (!req_frame) {
            co_return co_await on_storage_disconnect(conn);
        }

        auto handle = storage_req_handles.find((proto_cmd_e)req_frame->cmd);
        if (handle == storage_req_handles.end()) {
            g_log->log_warn(std::format("unhandle cmd {} from storage {}", req_frame->cmd, conn->to_string()));
            continue;
        }
        co_await handle->second(conn, req_frame);
    }

    co_return;
}
