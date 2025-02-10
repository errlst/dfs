#include "acceptor.h"
#include "./log.h"

namespace common {

acceptor::acceptor(asio::any_io_executor io, acceptor_conf conf)
    : acceptor_{io}, conf_{conf} {
  auto ep = asio::ip::tcp::endpoint(asio::ip::make_address(conf.ip), conf.port);
  acceptor_.open(ep.protocol());
  acceptor_.set_option(asio::socket_base::reuse_address(true));
  acceptor_.bind(ep);
  acceptor_.listen();
  if (!acceptor_.is_open()) {
    LOG_DEBUG(std::format("acceptor open failed"));
  }
  LOG_DEBUG(std::format("acceptor open suc"));
}

auto acceptor::accept() -> asio::awaitable<std::shared_ptr<connection>> {
  while (true) {
    auto sock = asio::ip::tcp::socket{co_await asio::this_coro::executor};
    auto [ec] = co_await acceptor_.async_accept(sock, asio::as_tuple(asio::use_awaitable));
    if (ec) {
      LOG_DEBUG(std::format("acceptor accept failed, {}", ec.message()));
      continue;
    }

    /* 建立心跳 */
    char request_to_send[sizeof(proto_frame) + sizeof(xx_heart_establish_request)];
    auto req_frame = (proto_frame *)request_to_send;
    *req_frame = {
        .cmd = (uint16_t)proto_cmd::xx_heart_establish,
        .type = REQUEST_FRAME,
        .data_len = sizeof(xx_heart_establish_request),
    };
    trans_frame_to_net(req_frame);
    *(xx_heart_establish_request *)req_frame->data = {
        .timeout = htonl(conf_.h_timeout),
        .interval = htonl(conf_.h_interval),
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
    if (ec_2 || n_2 != sizeof(response_recved) || response_recved.magic != FRAME_MAGIC || response_recved.cmd != (uint16_t)proto_cmd::xx_heart_establish || response_recved.stat != 0 || response_recved.data_len != 0) {
      LOG_DEBUG(std::format("acceptor recv establish heart failed, {} {}", ec_2.message(), n_2));
      sock.close();
      continue;
    }

    LOG_DEBUG(std::format("recv new connection"));
    auto conn = std::make_shared<connection>(std::move(sock));
    conn->h_timeout_ = conf_.h_timeout;
    conn->h_interval_ = conf_.h_interval;
    co_return conn;
  }
}

} // namespace common
