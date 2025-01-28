#include "acceptor.h"

acceptor_t::acceptor_t(const asio::any_io_executor &io, acceptor_conf_t conf)
    : m_acceptor{io}, m_conf{conf} {
    auto ep = asio::ip::tcp::endpoint(asio::ip::make_address(conf.ip), conf.port);
    m_acceptor.open(ep.protocol());
    m_acceptor.set_option(asio::socket_base::reuse_address(true));
    m_acceptor.bind(ep);
    m_acceptor.listen();
    m_as_string = std::format("listener {}:{}", conf.ip, conf.port);
}

auto acceptor_t::accept() -> asio::awaitable<std::shared_ptr<connection_t>> {
    while (true) {
        /* recv connection */
        auto [ec, sock] = co_await m_acceptor.async_accept(asio::as_tuple(asio::use_awaitable));
        if (ec) {
            m_conf.log->log_error(std::format("{} accept error {}", to_string(), ec.message()));
            continue;
        }

        /* 发送心跳建立请求 */
        char req_data[sizeof(proto_frame_t) + 8];
        auto req_frame = (proto_frame_t *)req_data;
        *req_frame = {
            .cmd = (uint8_t)proto_cmd_e::a_establish_heart,
            .data_len = 8,
        };
        *(uint32_t *)req_frame->data = m_conf.heart_timeout;
        *(uint32_t *)(req_frame->data + 4) = m_conf.heart_interval;
        auto [ec2, n] = co_await sock.async_write_some(asio::const_buffer(req_data, sizeof(req_data)), asio::as_tuple(asio::use_awaitable));
        if (ec2 || n != sizeof(proto_frame_t) + 8) {
            m_conf.log->log_error(std::format("{} send establish heart error {}", to_string(), ec2.message()));
            sock.close();
            continue;
        }

        /* 等待心跳建立响应 */
        auto res_frame = proto_frame_t{};
        auto [ec3, n3] = co_await sock.async_read_some(asio::mutable_buffer{&res_frame, sizeof(res_frame)}, asio::as_tuple(asio::use_awaitable));
        if (ec3 || n3 != sizeof(res_frame) || res_frame.frame_magic != FRAME_MAGIC || res_frame.cmd != (uint8_t)proto_cmd_e::a_establish_heart || res_frame.stat != 0) {
            m_conf.log->log_error(std::format("{} recv establish heart error {}", to_string(), ec3.message()));
            sock.close();
            continue;
        }

        auto conn = std::make_shared<connection_t>(std::move(sock), m_conf.log);
        conn->m_heart_timeout = m_conf.heart_timeout;
        conn->m_heart_interval = m_conf.heart_interval;
        m_conf.log->log_info(std::format("{} accept connection {} ", to_string(), conn->to_string()));
        co_return conn;
    }
}
