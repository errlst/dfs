#include "connection.h"
#include "timeout_token.h"

connection_t::connection_t(asio::ip::tcp::socket &&sock, connection_conf_t conf)
    : m_sock{std::move(sock)},
      m_conf{conf},
      m_ip{m_sock.remote_endpoint().address().to_string()},
      m_port{m_sock.remote_endpoint().port()} {
}

auto connection_t::start() -> asio::awaitable<void> {
    auto ex = co_await asio::this_coro::executor;
    asio::co_spawn(ex, start_heart(), asio::detached);
    asio::co_spawn(ex, start_recv(), asio::detached);
}

auto connection_t::recv_req_frame() -> asio::awaitable<std::shared_ptr<proto_frame_t>> {
    while (m_req_frames.empty()) {
        auto &timer = m_req_frame_waiters.emplace(co_await asio::this_coro::executor);
        timer.expires_after(std::chrono::hours{24 * 365});
        co_await timer.async_wait(asio::as_tuple(asio::use_awaitable));
        if (m_closed) {
            co_return nullptr;
        }
    }
    auto frame = m_req_frames.front();
    m_req_frames.pop();
    co_return frame;
}

auto connection_t::send_res_frame(std::shared_ptr<proto_frame_t> frame) -> asio::awaitable<bool> {
    co_return co_await do_send(frame.get(), sizeof(proto_frame_t) + frame->data_len);
}

auto connection_t::send_res_frame(proto_frame_t frame) -> asio::awaitable<bool> {
    co_return co_await do_send(&frame, sizeof(proto_frame_t) + frame.data_len);
}

auto connection_t::recv_res_frame(uint8_t frame_id) -> asio::awaitable<std::shared_ptr<proto_frame_t>> {
    /* TODO)) 暂时直接睡眠一段时间 */
    auto timer = asio::steady_timer{co_await asio::this_coro::executor};
    while (m_res_frames[frame_id] == nullptr) {
        timer.expires_after(std::chrono::milliseconds{100});
        co_await timer.async_wait(asio::use_awaitable);
        if (m_closed) {
            co_return nullptr;
        }
    }
    auto frame = m_res_frames[frame_id];
    m_res_frames[frame_id] = nullptr;
    co_return frame;
}

auto connection_t::send_req_frame(std::shared_ptr<proto_frame_t> frame) -> asio::awaitable<std::optional<uint8_t>> {
    auto id = m_frame_id++;
    frame->id = id;
    if (co_await do_send(frame.get(), sizeof(proto_frame_t) + frame->data_len)) {
        co_return id;
    }
    co_return std::nullopt;
}

auto connection_t::send_req_frmae(proto_frame_t frame) -> asio::awaitable<std::optional<uint8_t>> {
    auto id = m_frame_id++;
    frame.id = id;
    if (co_await do_send(&frame, sizeof(frame) + frame.data_len)) {
        co_return id;
    }
    co_return std::nullopt;
}

auto connection_t::do_send(void *data, uint64_t len) -> asio::awaitable<bool> {
    if (m_closed) {
        co_return false;
    }

    if (auto [ec, n] = co_await m_sock.async_write_some(asio::const_buffer(data, len), asio::as_tuple(asio::use_awaitable));
        ec || n != len) {
        auto errmsg = n != len ? "invliad send size" : ec.message();
        m_conf.log->log_error(std::format("send error from {}, errmsg {}", to_string(), errmsg));
        close();
        co_return false;
    }
    m_conf.log->log_debug(std::format("send frame to {} cmd: {} len: {} data_len: {}", to_string(), (uint16_t)((proto_frame_t *)data)->cmd, len, (uint32_t)((proto_frame_t *)data)->data_len));
    co_return true;
}

auto connection_t::close() -> void {
    m_sock.close();
    m_closed = true;
    while (!m_req_frame_waiters.empty()) {
        m_req_frame_waiters.front().cancel();
        m_req_frame_waiters.pop();
    }
}

auto connection_t::start_heart() -> asio::awaitable<void> {
    static auto heartbeat_frame = proto_frame_t{.cmd = static_cast<uint8_t>(proto_cmd_e::a_heart), .data_len = 0};
    static auto heartbeat_buffer = asio::const_buffer{&heartbeat_frame, sizeof(heartbeat_frame)};
    auto timer = asio::steady_timer{co_await asio::this_coro::executor};
    while (true) {
        timer.expires_after(std::chrono::milliseconds{m_conf.heartbeat_interval});
        co_await timer.async_wait(asio::use_awaitable);

        auto [ec, n] = co_await m_sock.async_write_some(heartbeat_buffer, asio::as_tuple(asio::use_awaitable));
        if (ec || n != sizeof(heartbeat_frame)) {
            auto errmsg = ec ? ec.message() : std::format("invliad send size: {}", n);
            m_conf.log->log_error(std::format("send error from {}, errmsg {}", to_string(), errmsg));
            close();
            co_return;
        }
        // m_conf.log->log_debug(std::format("send heartbeat to {}", to_string()));
        if (m_closed) {
            co_return;
        }
    }
}

auto connection_t::start_recv() -> asio::awaitable<void> {
    auto header_buf = proto_frame_t{};
    auto read_token = asio::as_tuple(asio::use_awaitable);
    auto read_timeout_token = timeout_token(read_token, m_conf.heartbeat_timeout);

    while (!m_closed) {
        /* 只在此处进行超时判断 */
        if (auto [ec, n] = co_await m_sock.async_read_some(asio::mutable_buffer{&header_buf, sizeof(header_buf)}, read_token);
            ec || n != sizeof(header_buf)) {
            auto errmsg = ec ? ec.message() : n == 0 ? "heartbeat timeout"
                                                     : "invliad send size";
            m_conf.log->log_error(std::format("recv error from {} {}", to_string(), errmsg));
            close();
            co_return;
        }
        m_conf.log->log_debug(std::format("recv frame from {} cmd: {} data_len: {}", to_string(), (uint16_t)header_buf.cmd, (uint32_t)header_buf.data_len));

        /* 校验 magic */
        if (header_buf.frame_magic != FRAME_MAGIC) {
            m_conf.log->log_error(std::format("invalid magic from {} {:#x}", to_string(), (uint16_t)header_buf.frame_magic));
            close();
            co_return;
        }

        /* 忽略 heartbeat */
        if (header_buf.cmd == (uint8_t)proto_cmd_e::a_heart) {
            // m_conf.log->log_debug(std::format("recv heart from {}", to_string()));
            continue;
        }

        /* 读取 payload */
        auto frame = std::shared_ptr<proto_frame_t>{(proto_frame_t *)malloc(sizeof(proto_frame_t) + header_buf.data_len), [](auto p) { free(p); }};
        if (frame == nullptr) {
            m_conf.log->log_error(std::format("alloc memory failed, requested size is {}MB", header_buf.data_len / 1024 / 1024));
            co_return;
        }
        memcpy(frame.get(), &header_buf, sizeof(proto_frame_t));
        if (auto [ec, n] = co_await m_sock.async_read_some(asio::mutable_buffer{reinterpret_cast<char *>(frame.get()) + sizeof(proto_frame_t), header_buf.data_len}, read_token);
            ec || n != header_buf.data_len) {
            auto errmsg = n != header_buf.data_len ? "invliad send size" : ec.message();
            m_conf.log->log_error(std::format("recv error from {}  {}", to_string(), errmsg));
            close();
            co_return;
        }

        /* 处理 request frame 和 response frame */
        if (frame->id >= m_frame_id) {
            m_frame_id = frame->id + 1;
            co_await slove_req_frame(frame);
        } else {
            co_await slove_res_frame(frame);
        }
    }
}

auto connection_t::slove_req_frame(std::shared_ptr<proto_frame_t> frame) -> asio::awaitable<void> {
    m_req_frames.push(frame);
    auto timer = std::move(m_req_frame_waiters.front());
    m_req_frame_waiters.pop();
    timer.cancel();
    co_return;
}

auto connection_t::slove_res_frame(std::shared_ptr<proto_frame_t> frame) -> asio::awaitable<void> {
    if (m_res_frames[frame->id] != nullptr) {
        close();
        co_return;
    }
    m_res_frames[frame->id] = frame;
}

auto connection_t::connect_to(asio::any_io_executor io, std::string_view ip, uint16_t port, connection_conf_t conf)
    -> asio::awaitable<std::tuple<std::shared_ptr<connection_t>, std::error_code>> {
    auto sock = asio::ip::tcp::socket{io};
    auto [ec] = co_await sock.async_connect(asio::ip::tcp::endpoint{asio::ip::make_address(ip), port}, asio::as_tuple(asio::use_awaitable));
    if (ec) {
        co_return std::make_tuple(nullptr, ec);
    }
    auto conn = std::make_shared<connection_t>(std::move(sock), conf);
    asio::co_spawn(io, conn->start(), asio::detached);
    co_return std::make_tuple(conn, std::error_code{});
}
