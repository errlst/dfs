#include "../common/connection.h"
#include "../common/protocol.h"
#include "../common/util.h"
#include "../proto/proto.h"
#include "./util.h"
#include <fstream>

auto g_log = std::make_shared<log_t>("./log", log_level_e::debug, false);
auto g_conn = std::shared_ptr<connection_t>{};

auto upload_fille(std::string_view path) -> asio::awaitable<void> {
    /* get file size */
    auto ifs = std::ifstream{std::string{path}, std::ios::binary};
    if (!ifs) {
        g_log->log_error(std::format("failed to open file {}", path));
        co_return;
    }
    ifs.seekg(0, std::ios::end);
    auto size = ifs.tellg();
    ifs.seekg(0, std::ios::beg);

    /* request */
    auto req_frame = (proto_frame_t *)malloc(sizeof(proto_frame_t) + sizeof(uint64_t));
    *req_frame = {
        .cmd = (uint8_t)proto_cmd_e::cm_valid_storage,
        .data_len = sizeof(uint64_t),
    };
    *((uint64_t *)req_frame->data) = size;
    auto id = co_await g_conn->send_req_frame(std::shared_ptr<proto_frame_t>{req_frame, [](auto p) { free(p); }});
    if (!id) {
        g_log->log_error("failed to send cm_valid_storage request");
        co_return;
    }

    /* response */
    auto res_frame = co_await g_conn->recv_res_frame(id.value());
    if (!res_frame) {
        g_log->log_error("failed to recv cm_valid_storage response");
        co_return;
    }
    if (res_frame->stat) {
        g_log->log_error(std::format("cm_valid_storage response stat {}", res_frame->stat));
        co_return;
    }
    auto res_data = dfs::proto::cm_valid_storage::response_t{};
    if (!res_data.ParseFromArray(res_frame->data, res_frame->data_len)) {
        g_log->log_error("failed to parse cm_valid_storage response");
        co_return;
    }
    g_log->log_info(std::format("get valid storage {}:{} {}", res_data.storage_ip(), res_data.storage_port(), res_data.storage_magic()));

    /* connect to storage */
}

auto client() -> asio::awaitable<void> {
    g_conn = co_await connect_to_master("127.0.0.1", 8888, g_log);

    auto cmd = std::string{};
    while (true) {
        std::cout << ": " << std::flush;
        std::cin >> cmd;
        if (cmd == "upload") {
            std::string path;
            std::cin >> path;
            co_await upload_fille(path);
        }
    }
}

auto main() -> int {
    auto io = asio::io_context{};
    auto guard = asio::make_work_guard(io);
    asio::co_spawn(io, client(), asio::detached);
    io.run();
    return 0;
}