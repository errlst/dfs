module;

#include "log.h"
#include <asio.hpp>
#include <cstdint>
#include <string>

module common.acceptor;

namespace common {

  acceptor::acceptor(asio::any_io_executor io,
                     const std::string &ip, uint16_t port,
                     uint32_t heart_timeout, uint32_t heart_interval) try
      : m_acceptor{io}, m_heart_timeout{heart_timeout}, m_heart_interval{heart_interval} {
    auto ep = asio::ip::tcp::endpoint(asio::ip::make_address(ip), port);
    m_acceptor.open(ep.protocol());
    m_acceptor.set_option(asio::socket_base::reuse_address(true));
    m_acceptor.bind(ep);
    m_acceptor.listen();
    if (!m_acceptor.is_open()) {
      LOG_DEBUG(std::format("acceptor open failed"));
      exit(-1);
    }
    LOG_INFO(std::format("accept at {}:{}", ip, port));
  } catch (const std::runtime_error &ec) {
    LOG_CRITICAL("acceptor init failed, {}", ec.what());
    exit(-1);
  }

  auto acceptor::accept() -> asio::awaitable<std::shared_ptr<connection>> {
    while (true) {
      auto sock = asio::ip::tcp::socket{co_await asio::this_coro::executor};
      auto [ec] = co_await m_acceptor.async_accept(sock, asio::as_tuple(asio::use_awaitable));
      if (ec) {
        LOG_DEBUG(std::format("acceptor accept failed, {}", ec.message()));
        continue;
      }

      /* 建立心跳 */
      char request_to_send[sizeof(proto_frame) + sizeof(xx_heart_establish_request)];
      auto req_frame = (proto_frame *)request_to_send;
      *req_frame = {
          .cmd = proto_cmd::xx_heart_establish,
          .type = frame_type::request,
          .data_len = sizeof(xx_heart_establish_request),
      };
      trans_frame_to_net(req_frame);
      *(xx_heart_establish_request *)req_frame->data = {
          .timeout = htonl(m_heart_timeout),
          .interval = htonl(m_heart_interval),
      };
      auto [ec_1, n_1] = co_await asio::async_write(sock, asio::const_buffer(request_to_send, sizeof(request_to_send)), asio::as_tuple(asio::use_awaitable));
      if (ec_1 || n_1 != sizeof(request_to_send)) {
        LOG_DEBUG(std::format("acceptor send establish heart failed, {} {}", ec_1.message(), n_1));
        sock.close();
        continue;
      }

      auto response_recved = proto_frame{};
      auto [ec_2, n_2] = co_await asio::async_read(sock, asio::mutable_buffer{&response_recved, sizeof(response_recved)}, asio::as_tuple(asio::use_awaitable));
      trans_frame_to_host(&response_recved);
      if (ec_2 || n_2 != sizeof(response_recved) || response_recved.magic != FRAME_MAGIC || response_recved.cmd != proto_cmd::xx_heart_establish || response_recved.stat != 0 || response_recved.data_len != 0) {
        LOG_DEBUG(std::format("acceptor recv establish heart failed, {} {}", ec_2.message(), n_2));
        sock.close();
        continue;
      }

      auto conn = std::make_shared<connection>(std::move(sock), m_heart_timeout, m_heart_interval);
      LOG_INFO(std::format("accept new connection {}", conn->address()));
      co_return conn;
    }
  }
  
} // namespace common