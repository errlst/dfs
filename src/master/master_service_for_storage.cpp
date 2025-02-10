#include "./master_service_global.h"

// /* 定期更新 storage 可用空间 */
// static auto update_storage_free_disk_impl(std::shared_ptr<connection> conn) -> asio::awaitable<bool> {
//     auto id = co_await conn->send_req_frame(proto_frame_t{.cmd = (uint8_t)proto_cmd_e::ms_fs_free_size});
//     if (!id) {
//         co_return false;
//     }

//     auto res_frame = co_await conn->recv_res_frame(id.value());
//     if (!res_frame) {
//         co_return false;
//     }

//     auto useable_disk = *(uint64_t *)res_frame->data;
//     // g_log->log_debug(std::format("storage {} max useable disk {}", conn->to_string(), useable_disk));
//     conn->set_data(conn_data::s_free_disk, useable_disk);
//     co_return true;
// }

// static auto update_storage_free_disk(std::shared_ptr<connection> conn) -> asio::awaitable<void> {
//     while (co_await update_storage_free_disk_impl(conn)) {
//         co_await co_sleep_for(std::chrono::seconds{5});
//     }
// }

// auto on_storage_disconnect(std::shared_ptr<connection> conn) -> asio::awaitable<void> {
//     auto id = conn->get_data<uint32_t>(conn_data::s_id).value();
//     unregist_storage(id);

//     auto ip = conn->get_data<std::string>(conn_data::s_ip);
//     auto port = conn->get_data<uint32_t>(conn_data::s_port);
//     if (!ip.has_value() || !port.has_value()) {
//         g_m_log->log_fatal("failed to get storage ip or port");
//         exit(-1);
//     }
//     g_m_log->log_warn(std::format("storage {}:{} disconnect", ip.value(), port.value()));

//     co_return;
// }





// std::map<uint8_t, req_handle_t> storage_req_handles{};

// auto recv_from_storage(std::shared_ptr<connection> conn) -> asio::awaitable<void> {
//     asio::co_spawn(co_await asio::this_coro::executor, update_storage_free_disk(conn), asio::detached);
//     while (true) {
//         auto req_frame = co_await conn->recv_req_frame();
//         if (!req_frame) {
//             co_await on_storage_disconnect(conn);
//             co_return;
//         }

//         if (storage_req_handles.contains(req_frame->cmd)) {
//             co_await storage_req_handles[req_frame->cmd](conn, req_frame);
//         } else {
//             g_m_log->log_error(std::format("unknown storage cmd {}", req_frame->cmd));
//         }
//     }
// }
