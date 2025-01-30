#include "./master_service_global.h"

auto on_client_disconnect(std::shared_ptr<connection_t> conn) -> asio::awaitable<void> {
    g_log->log_warn(std::format("client {} disconnect", conn->to_string()));
    co_return;
}

auto cm_valid_storage_handle(std::shared_ptr<connection_t> conn, std::shared_ptr<proto_frame_t> req_frame) -> asio::awaitable<void> {
    /* get a valid storage */
    auto valid_storage = std::shared_ptr<connection_t>{};
    for (auto i = 0; i < storage_conns_vec.size(); ++i) {
        auto storage = next_storage();
        auto usable_disk = storage->get_data<uint64_t>(conn_data::s_free_disk).value();
        if (usable_disk > *(uint64_t *)req_frame->data) {
            valid_storage = storage;
            break;
        }
    }
    if (!valid_storage) {
        g_log->log_warn(std::format("not find valid storage for size {}", *(uint64_t *)req_frame->data));
        co_await conn->send_res_frame(
            proto_frame_t{
                .cmd = req_frame->cmd,
                .stat = 1,
            },
            req_frame->id);
        co_return;
    }

    /* response */
    auto res_data = dfs::proto::cm_valid_storage::response_t{};
    res_data.set_storage_magic(valid_storage->get_data<uint32_t>(conn_data::s_magic).value());
    res_data.set_storage_port(valid_storage->get_data<uint32_t>(conn_data::s_port).value());
    res_data.set_storage_ip(valid_storage->get_data<std::string>(conn_data::s_ip).value());
    auto res_frame = (proto_frame_t *)malloc(sizeof(proto_frame_t) + res_data.ByteSizeLong());
    *res_frame = {
        .id = req_frame->id,
        .cmd = req_frame->cmd,
        .data_len = (uint32_t)res_data.ByteSizeLong(),
    };
    if (!res_data.SerializeToArray(res_frame->data, res_frame->data_len)) {
        g_log->log_error("failed to serialize cm_valid_storage response");
        co_await conn->send_res_frame(
            proto_frame_t{
                .cmd = req_frame->cmd,
                .stat = 2,
            },
            req_frame->id);
        co_return;
    }
    co_await conn->send_res_frame(std::shared_ptr<proto_frame_t>{res_frame, [](auto p) { free(p); }}, req_frame->id);
}

auto cm_group_storages_handle(std::shared_ptr<connection_t> conn, std::shared_ptr<proto_frame_t> req_frame) -> asio::awaitable<void> {
    if (req_frame->data_len != sizeof(uint32_t)) {
        co_await conn->send_res_frame(
            proto_frame_t{
                .cmd = req_frame->cmd,
                .stat = 1,
            },
            req_frame->id);
        co_return;
    }

    auto res_data = dfs::proto::cm_group_storages::response_t{};
    for (const auto &storage : group_storages(*(uint32_t *)req_frame->data)) {
        auto info = res_data.add_storages();
        info->set_ip(storage->get_data<std::string>(conn_data::s_ip).value());
        info->set_port(storage->get_data<uint32_t>(conn_data::s_port).value());
        info->set_magic(storage->get_data<uint32_t>(conn_data::s_magic).value());
    }
    auto res_frame = (proto_frame_t *)malloc(sizeof(proto_frame_t) + res_data.ByteSizeLong());
    *res_frame = {
        .id = req_frame->id,
        .cmd = req_frame->cmd,
        .data_len = (uint32_t)res_data.ByteSizeLong(),
    };
    if (!res_data.SerializeToArray(res_frame->data, res_frame->data_len)) {
        g_log->log_error("failed to serialize cm_group_storages response");
        co_await conn->send_res_frame(
            proto_frame_t{
                .cmd = req_frame->cmd,
                .stat = 2,
            },
            req_frame->id);
        co_return;
    }
    co_await conn->send_res_frame(std::shared_ptr<proto_frame_t>{res_frame, [](auto p) { free(p); }}, req_frame->id);
    co_return;
}

std::map<uint8_t, req_handle_t> client_req_handles{
    {(uint8_t)proto_cmd_e::cm_valid_storage, cm_valid_storage_handle},
    {(uint8_t)proto_cmd_e::cm_group_storages, cm_group_storages_handle},
};

auto recv_from_client(std::shared_ptr<connection_t> conn) -> asio::awaitable<void> {
    while (true) {
        auto req_frame = co_await conn->recv_req_frame();
        if (!req_frame) {
            co_await on_client_disconnect(conn);
            co_return;
        }

        /* storage regist if suc, storage conn will run in read_from_storag */
        if (req_frame->cmd == (uint8_t)proto_cmd_e::sm_regist) {
            co_await sm_regist_handle(conn, req_frame);
            co_return;
        }

        /* call cmd handle */
        if (client_req_handles.contains(req_frame->cmd)) {
            co_await client_req_handles[req_frame->cmd](conn, req_frame);
        } else {
            g_log->log_error(std::format("unknown client cmd {}", req_frame->cmd));
        }
    }
}