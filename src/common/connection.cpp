#include "connection.h"
#include "./log.h"
#include "./util.h"
#include <asio/experimental/parallel_group.hpp>

namespace common {

connection::connection(asio::ip::tcp::socket &&sock)
    : sock_{std::move(sock)},
      strand_(asio::make_strand(sock_.get_executor())) {
}

connection::~connection() {
  LOG_ERROR(std::format("connection destoryed"));
  sock_.close();
}

auto connection::start(std::function<asio::awaitable<void>(std::shared_ptr<proto_frame>, std::shared_ptr<connection>)> on_recv_request) -> void {
  on_recv_request_ = on_recv_request;
  asio::co_spawn(strand_, start_recv(), asio::detached);
  asio::co_spawn(strand_, start_heart(), asio::detached);
}

auto connection::close() -> asio::awaitable<void> {
  if (closed_) {
    co_return;
  }
  closed_ = true;
  for (auto &_ : response_frames_) {
    _.second.second->cancel();
  }
  if (heart_timer_) {
    heart_timer_->cancel();
  }
  co_await on_recv_request_(nullptr, shared_from_this());
  for (auto on_close : on_close_works_) {
    on_close();
  }
}

auto connection::recv_response(uint16_t id) -> asio::awaitable<std::shared_ptr<proto_frame>> {
  co_await asio::post(strand_, asio::use_awaitable);
  LOG_DEBUG(std::format("waiting response {}", id));
  auto &[frame, waiter] = response_frames_[id];
  if (frame == nullptr) {
    waiter = std::make_shared<asio::steady_timer>(strand_, std::chrono::days{365});
    co_await waiter->async_wait(asio::as_tuple(asio::use_awaitable));
    if (closed_) {
      co_return nullptr;
    }
  }
  auto ret = frame;
  response_frames_.erase(id);
  co_return ret;
}

auto connection::send_request(proto_frame *frame) -> asio::awaitable<std::optional<uint16_t>> {
  co_await asio::post(strand_, asio::use_awaitable);
  frame->id = request_frame_id_++;
  frame->type = REQUEST_FRAME;
  auto frame_len = sizeof(proto_frame) + frame->data_len;
  auto untransed_frame = *frame;
  trans_frame_to_net(frame);
  auto [ec, n] = co_await asio::async_write(sock_, asio::const_buffer(frame, frame_len), asio::as_tuple(asio::use_awaitable));
  if (n != frame_len) {
    LOG_DEBUG(std::format("send request failed {}", ec.message()));
    co_await close();
    co_return std::nullopt;
  }
  if (frame->magic != 0x55aa) {
    LOG_DEBUG("");
  }
  LOG_DEBUG(std::format(R"(send request : {{
    magic: {:x},
    id: {},
    cmd: {},
    type: {},
    stat: {},
    data_len: {}
  }})",
                        untransed_frame.magic, untransed_frame.id, untransed_frame.cmd, untransed_frame.type, untransed_frame.stat, frame_len - sizeof(proto_frame)));

  co_return untransed_frame.id;
}

auto connection::send_request(proto_frame frame) -> asio::awaitable<std::optional<uint16_t>> {
  co_await asio::post(strand_, asio::use_awaitable);
  if (closed_) {
    co_return std::nullopt;
  }

  frame.id = request_frame_id_++;
  frame.type = REQUEST_FRAME;
  frame.data_len = 0;
  auto untransed_frame = frame;
  trans_frame_to_net(&frame);
  auto [ec, n] = co_await asio::async_write(sock_, asio::const_buffer(&frame, sizeof(proto_frame)), asio::as_tuple(asio::use_awaitable));
  if (n != sizeof(proto_frame)) {
    LOG_DEBUG(std::format("send request failed {}", ec.message()));
    co_await close();
    co_return std::nullopt;
  }
  LOG_DEBUG(std::format(R"(send request : {{
    magic: {:x},
    id: {},
    cmd: {},
    type: {},
    stat: {},
    data_len: {}
  }})",
                        untransed_frame.magic, untransed_frame.id, untransed_frame.cmd, untransed_frame.type, untransed_frame.stat, 0));

  co_return untransed_frame.id;
}

auto connection::send_response(proto_frame *frame, std::shared_ptr<proto_frame> req_frame) -> asio::awaitable<bool> {
  co_await asio::post(strand_, asio::use_awaitable);
  if (closed_) {
    co_return false;
  }

  frame->id = req_frame->id;
  frame->type = RESPONSE_FRAME;
  frame->cmd = req_frame->cmd;
  auto untransed_frame = *frame;
  auto frame_len = sizeof(proto_frame) + frame->data_len;
  trans_frame_to_net(frame);
  auto [ec, n] = co_await asio::async_write(sock_, asio::const_buffer(frame, frame_len), asio::as_tuple(asio::use_awaitable));
  if (n != frame_len) {
    LOG_ERROR(std::format("send response failed {}", ec.message()));
    co_await close();
    co_return false;
  }
  LOG_DEBUG(std::format(R"(send response : {{
    magic: {:x},
    id: {},
    cmd: {},
    type: {},
    stat: {},
    data_len: {}
  }})",
                        untransed_frame.magic, untransed_frame.id, untransed_frame.cmd, untransed_frame.type, untransed_frame.stat, frame_len - sizeof(proto_frame)));
  co_return true;
}

auto connection::send_response(proto_frame frame, std::shared_ptr<proto_frame> req_frame) -> asio::awaitable<bool> {
  co_await asio::post(strand_, asio::use_awaitable);
  if (closed_) {
    co_return false;
  }

  frame.id = req_frame->id;
  frame.type = RESPONSE_FRAME;
  frame.cmd = req_frame->cmd;
  auto untransed_frame = frame;
  trans_frame_to_net(&frame);
  auto [ec, n] = co_await asio::async_write(sock_, asio::const_buffer(&frame, sizeof(proto_frame)), asio::as_tuple(asio::use_awaitable));
  if (n != sizeof(proto_frame)) {
    LOG_ERROR(std::format("send response failed {}", ec.message()));
    co_await close();
    co_return false;
  }
  LOG_DEBUG(std::format(R"(send response : {{
    magic: {:x},
    id: {},
    cmd: {},
    type: {},
    stat: {},
    data_len: {}
  }})",
                        untransed_frame.magic, untransed_frame.id, untransed_frame.cmd, untransed_frame.type, untransed_frame.stat, 0));
  co_return true;
}

auto connection::add_exetension_work(std::function<asio::awaitable<void>(std::shared_ptr<connection>)> work, std::function<void()> on_close) -> void {
  asio::co_spawn(strand_, work(shared_from_this()), asio::detached);
  on_close_works_.emplace_back(on_close);
}

auto connection::ip() -> std::string {
  return sock_.remote_endpoint().address().to_string();
}

auto connection::port() -> uint16_t {
  return sock_.remote_endpoint().port();
}

auto connection::connect_to(std::string_view ip, uint16_t port) -> asio::awaitable<std::shared_ptr<connection>> {
  auto sock = asio::ip::tcp::socket{co_await asio::this_coro::executor};
  auto [ec] = co_await sock.async_connect(asio::ip::tcp::endpoint{asio::ip::make_address(ip), port}, asio::as_tuple(asio::use_awaitable));
  if (ec) {
    LOG_ERROR(std::format("connect to {}:{} failed {}", ip, port, ec.message()));
    co_return nullptr;
  }

  /* 建立心跳 */
  char request[sizeof(proto_frame) + sizeof(xx_heart_establish_request)];
  auto [ec_, n] = co_await asio::async_read(sock, asio::mutable_buffer(&request, sizeof(request)), asio::as_tuple(asio::use_awaitable));
  if (n != sizeof(request)) {
    LOG_ERROR(std::format("recv establish heart failed {}", ec_.message()));
    sock.close();
    co_return nullptr;
  }

  auto req_header = (proto_frame *)request;
  trans_frame_to_host(req_header);
  if (req_header->magic != FRAME_MAGIC || req_header->cmd != (uint16_t)proto_cmd::xx_heart_establish) {
    LOG_ERROR(std::format("recv establish heart failed, invalid frame"));
    sock.close();
    co_return nullptr;
  }

  auto res_header = proto_frame{.cmd = (uint16_t)proto_cmd::xx_heart_establish};
  trans_frame_to_host(&res_header);
  std::tie(ec, n) = co_await asio::async_write(sock, asio::const_buffer(&res_header, sizeof(res_header)), asio::as_tuple(asio::use_awaitable));
  if (n != sizeof(proto_frame)) {
    LOG_ERROR(std::format("send establish heart failed {}", ec.message()));
    sock.close();
    co_return nullptr;
  }

  auto req_data = (xx_heart_establish_request *)req_header->data;
  auto conn = std::make_shared<connection>(std::move(sock));
  conn->h_timeout_ = ntohl(req_data->timeout);
  conn->h_interval_ = ntohl(req_data->interval);
  co_return conn;
}

auto connection::start_heart() -> asio::awaitable<void> {
  auto frame = proto_frame{.cmd = (uint8_t)proto_cmd::xx_heart_ping};
  trans_frame_to_net(&frame);
  heart_timer_ = std::make_shared<asio::steady_timer>(strand_);
  while (true) {
    heart_timer_->expires_after(std::chrono::milliseconds{h_interval_});
    co_await heart_timer_->async_wait(asio::as_tuple(asio::use_awaitable));
    if (closed_) {
      // LOG_DEBUG("heart ping stoped");
      co_return;
    }

    if (auto [ec, n] = co_await asio::async_write(sock_, asio::const_buffer(&frame, sizeof(frame)), asio::as_tuple(asio::use_awaitable));
        ec || n != sizeof(frame)) {
      LOG_DEBUG(std::format("heart ping failed {}", ec.message()));
      co_await close();
      co_return;
    }
    // LOG_DEBUG(std::format("send heart ping"));
  }
}

auto connection::start_recv() -> asio::awaitable<void> {
  auto frame_header = proto_frame{};
  while (!closed_) {
    /* 超时异步读取 */
    auto timer = asio::steady_timer{strand_, std::chrono::milliseconds{h_timeout_}};
    timer.async_wait([this](const asio::error_code &ec) {
      if (!ec) {
        sock_.cancel();
        LOG_DEBUG(std::format("recv frame timeout"));
      } else {
        // LOG_DEBUG(std::format("timer cancel {}", ec.message()));
      }
    });
    auto [ec, n] = co_await asio::async_read(sock_, asio::mutable_buffer(&frame_header, sizeof(proto_frame)), asio::as_tuple(asio::use_awaitable));
    if (n != sizeof(proto_frame)) {
      LOG_DEBUG(std::format("recv frame failed {}", ec.message()));
      timer.cancel();
      co_await close();
      co_return;
    }
    trans_frame_to_host(&frame_header);
    if (frame_header.cmd != (uint16_t)proto_cmd::xx_heart_ping) {
      LOG_DEBUG(std::format(R"(recv frame : {{
        magic: {:x},
        id: {},
        cmd: {},
        type: {},
        stat: {},
        data_len: {}
      }})",
                            frame_header.magic, frame_header.id, frame_header.cmd, frame_header.type, frame_header.stat, frame_header.data_len));
    }

    /* 校验 magic */
    if (frame_header.magic != FRAME_MAGIC || (frame_header.type != REQUEST_FRAME && frame_header.type != RESPONSE_FRAME)) {
      LOG_DEBUG(std::format("recv invalid magic {:#x}", frame_header.magic));
      co_await close();
      co_return;
    }

    // /* 忽略 heartbeat */
    if (frame_header.cmd == (uint16_t)proto_cmd::xx_heart_ping) {
      // LOG_DEBUG(std::format("recv heart ping"));
      continue;
    }

    /* 读取 payload */
    auto frame = std::shared_ptr<proto_frame>{(proto_frame *)malloc(sizeof(proto_frame) + frame_header.data_len), [](auto p) { 
      // LOG_DEBUG(std::format("frame destoryed"));
      free(p); }};
    if (frame == nullptr) {
      LOG_DEBUG(std::format("alloc memory failed, requested size is {}MB", frame_header.data_len / 1024 / 1024));
      co_return;
    }
    std::tie(ec, n) = co_await asio::async_read(sock_, asio::mutable_buffer(frame.get()->data, frame_header.data_len), asio::as_tuple(asio::use_awaitable));
    if (n != frame_header.data_len) {
      LOG_DEBUG(std::format("recv payload failed {}", ec.message()));
      co_await close();
      co_return;
    }
    *frame = frame_header;

    if (frame->type == REQUEST_FRAME) {
      LOG_DEBUG(std::format("recv request cmd: {}", frame->cmd));
      asio::co_spawn(strand_, on_recv_request_(frame, shared_from_this()), asio::detached);
    } else if (frame->type == RESPONSE_FRAME) {
      auto &[res_frame, waiter] = response_frames_[frame->id];
      res_frame = frame;
      if (waiter != nullptr) {
        waiter->cancel();
      }
      // LOG_DEBUG(std::format("recv response frame id: {}", frame->id));
    }
  }
}
} // namespace common