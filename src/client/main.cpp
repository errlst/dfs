#include "../common/connection.h"
#include "../common/protocol.h"
#include "../common/util.h"
#include "../proto/proto.h"
#include <fstream>

auto g_log = std::make_shared<log_t>("./log", log_level_e::debug, false);
auto g_conn = std::shared_ptr<connection_t>{};

auto upload_file(std::string_view path) -> asio::awaitable<void> {
    g_log->log_debug(std::format("{}", std::filesystem::current_path().string()));
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
    auto storage_conn = co_await connection_t::connect_to(res_data.storage_ip(), res_data.storage_port(), g_log);
    if (!storage_conn) {
        g_log->log_error(std::format("failed to connect to storage {}:{} {}", res_data.storage_ip(), res_data.storage_port(), res_data.storage_magic()));
        co_return;
    }
    g_log->log_info(std::format("connect to storage {}:{} suc", res_data.storage_ip(), res_data.storage_port()));

    /* create fille request */
    req_frame = (proto_frame_t *)malloc(sizeof(proto_frame_t) + sizeof(uint64_t));
    *req_frame = {
        .cmd = (uint8_t)proto_cmd_e::cs_create_file,
        .data_len = sizeof(uint64_t),
    };
    *((uint64_t *)req_frame->data) = size;
    id = co_await storage_conn->send_req_frame(std::shared_ptr<proto_frame_t>{req_frame, [](auto p) { free(p); }});
    if (!id) {
        g_log->log_error("failed to send cs_create_file request");
        co_return;
    }

    /* wait response */
    res_frame = co_await storage_conn->recv_res_frame(id.value());
    if (!res_frame) {
        g_log->log_error("failed to recv cs_create_file response");
        co_return;
    }

    /* upload files 5MB 为一个片段 */
    auto req_frame_2 = std::shared_ptr<proto_frame_t>{(proto_frame_t *)malloc(sizeof(proto_frame_t) + 5 * 1024 * 1024), [](auto p) { free(p); }};
    *req_frame_2 = {
        .cmd = (uint8_t)proto_cmd_e::cs_upload_file,
    };
    while (ifs && !ifs.eof()) {
        auto n = ifs.readsome(req_frame_2->data, 5 * 1024 * 1024);
        if (n == 0) {
            break;
        }
        req_frame_2->data_len = n;
        id = co_await storage_conn->send_req_frame(req_frame_2);
        if (!id) {
            g_log->log_error("failed to send cs_append_data request");
            co_return;
        }

        res_frame = co_await storage_conn->recv_res_frame(id.value());
        if (!res_frame) {
            g_log->log_error("failed to recv cs_append_data response");
            co_return;
        }
        if (res_frame->stat) {
            g_log->log_error(std::format("cs_append_data response stat {}", res_frame->stat));
            co_return;
        }
        g_log->log_info(std::format("upload {} bytes", n));
    }

    /* close file */
    auto file_name = path.substr(path.find_last_of('/') + 1);
    auto req_frame_3 = (proto_frame_t *)malloc(sizeof(proto_frame_t) + file_name.size());
    *req_frame_3 = {
        .cmd = (uint8_t)proto_cmd_e::cs_close_file,
        .data_len = (uint32_t)file_name.size(),
    };
    std::memcpy(req_frame_3->data, file_name.data(), file_name.size());
    id = co_await storage_conn->send_req_frame(std::shared_ptr<proto_frame_t>{req_frame_3, [](auto p) { free(p); }});
    if (!id) {
        g_log->log_error("failed to send cs_close_file request");
        co_return;
    }

    res_frame = co_await storage_conn->recv_res_frame(id.value());
    if (!res_frame) {
        g_log->log_error("failed to recv cs_close_file response");
        co_return;
    }
    if (res_frame->stat) {
        g_log->log_error(std::format("cs_close_file response stat {}", res_frame->stat));
        co_return;
    }

    g_log->log_info(std::format("upload suc file: {}", std::string_view{res_frame->data, res_frame->data_len}));
    co_return;
}

auto download_file(std::string_view src, std::string_view dst) -> asio::awaitable<void> {
    auto ofs = std::ofstream{std::string{dst}, std::ios::binary};
    if (!ofs) {
        g_log->log_error(std::format("failed to open file {}", dst));
        co_return;
    }

    /* request storages */
    auto req_frame = (proto_frame_t *)malloc(sizeof(proto_frame_t) + sizeof(uint32_t));
    *req_frame = {
        .cmd = (uint8_t)proto_cmd_e::cm_group_storages,
        .data_len = sizeof(uint32_t),
    };
    *((uint32_t *)req_frame->data) = std::atol(src.substr(0, src.find_first_of('/')).data());
    auto id = co_await g_conn->send_req_frame(std::shared_ptr<proto_frame_t>{req_frame, [](auto p) { free(p); }});
    if (!id) {
        g_log->log_error("failed to send cm_group_storages request");
        co_return;
    }

    /*  */
    auto res_frame = co_await g_conn->recv_res_frame(id.value());
    if (!res_frame) {
        g_log->log_error("failed to recv cm_group_storages response");
        co_return;
    }
    if (res_frame->stat) {
        g_log->log_error(std::format("cm_group_storages response stat {}", res_frame->stat));
        co_return;
    }
    auto res_data = dfs::proto::cm_group_storages::response_t{};
    if (!res_data.ParseFromArray(res_frame->data, res_frame->data_len)) {
        g_log->log_error("failed to parse cm_group_storages response");
        co_return;
    }

    /* 遍历 storage 获取有效的 connection */
    auto storage_conn = std::shared_ptr<connection_t>{};
    for (const auto &s_info : res_data.storages()) {
        storage_conn = co_await connection_t::connect_to(s_info.ip(), s_info.port(), g_log);
        if (!storage_conn) {
            g_log->log_error(std::format("failed to connect to storage {}:{}", s_info.ip(), s_info.port()));
            continue;
        }

        /* request open file */
        req_frame = (proto_frame_t *)malloc(sizeof(proto_frame_t) + src.size());
        *req_frame = {
            .cmd = (uint8_t)proto_cmd_e::cs_open_file,
            .data_len = (uint32_t)src.size(),
        };
        std::copy(src.begin(), src.end(), req_frame->data);
        id = co_await storage_conn->send_req_frame(std::shared_ptr<proto_frame_t>{req_frame, [](auto p) { free(p); }});
        if (!id) {
            g_log->log_error("failed to send cs_open_file request");
            continue;
        }

        /* wait response */
        res_frame = co_await storage_conn->recv_res_frame(id.value());
        if (!res_frame) {
            g_log->log_error("failed to recv cs_open_file response");
            storage_conn = nullptr;
            continue;
        }
        if (res_frame->stat) {
            g_log->log_error(std::format("cs_open_file response stat {}", res_frame->stat));
            storage_conn = nullptr;
            continue;
        }
        break;
    }

    if (!storage_conn) {
        co_return;
    }

    g_log->log_info(std::format("watting download from storage {} file_size is {}", storage_conn->to_string(), *((uint64_t *)res_frame->data)));

    while (true) {
        /* 每次下载 5MB */
        req_frame = (proto_frame_t *)malloc(sizeof(proto_frame_t) + sizeof(uint32_t));
        *req_frame = {
            .cmd = (uint8_t)proto_cmd_e::cs_download_file,
            .data_len = sizeof(uint32_t),
        };
        *((uint32_t *)req_frame->data) = 5 * 1024 * 1024;
        id = co_await storage_conn->send_req_frame(std::shared_ptr<proto_frame_t>{req_frame, [](auto p) { free(p); }});
        if (!id) {
            g_log->log_error("failed to send cs_download_file request");
            break;
        }

        res_frame = co_await storage_conn->recv_res_frame(id.value());
        if (!res_frame) {
            g_log->log_error("failed to recv cs_download_file response");
            break;
        }
        if (res_frame->stat) {
            g_log->log_error(std::format("cs_download_file response stat {}", res_frame->stat));
            break;
        }
        if (res_frame->data_len == 0) {
            g_log->log_info("download suc");
            ofs.flush();
            break;
        }

        ofs.write(res_frame->data, res_frame->data_len);
        g_log->log_info(std::format("download {} bytes", (uint32_t)res_frame->data_len));
    }
}

auto client() -> asio::awaitable<void> {
    g_conn = co_await connection_t::connect_to("127.0.0.1", 8888, g_log);
    if (!g_conn) {
        g_log->log_error("failed to connect to master");
        co_return;
    }

    // auto cmd = std::string{};
    // while (true) {
    //     std::cout << ": " << std::flush;
    //     std::cin >> cmd;
    //     if (cmd == "upload") {
    //         std::string path;
    //         std::cin >> path;
    //         co_await upload_file(path);
    //     } else if (cmd == "download") {
    //         std::string src, dst;
    //         std::cin >> src >> dst;
    //         co_await download_file(src, dst);
    //     } else if (cmd == "connect") {
    //         std::string ip;
    //         uint32_t port;
    //         std::cin >> ip >> port;
    //         auto conn = co_await connection_t::connect_to(ip, port, g_log);
    //         std::cin >> ip;
    //     }
    // }
}

auto main() -> int {
    auto io = asio::io_context{};
    auto guard = asio::make_work_guard(io);
    asio::co_spawn(io, client(), asio::detached);
    std::thread{[&] {
        io.run();
    }}.detach();

    auto cmd = std::string{};
    while (true) {
        std::cout << ": " << std::flush;
        std::cin >> cmd;
        if (cmd == "upload") {
            std::string path;
            std::cin >> path;
            asio::co_spawn(io, upload_file(path), asio::detached);
        } else if (cmd == "download") {
            std::string src, dst;
            std::cin >> src >> dst;
            asio::co_spawn(io, download_file(src, dst), asio::detached);
        } else if (cmd == "connect") {
            std::string ip;
            uint32_t port;
            std::cin >> ip >> port;
        }
    }

    return 0;
}