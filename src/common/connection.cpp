#include "connection.h"
#include "./log.h"
#include "exception_handle.h"
#include <asio/experimental/parallel_group.hpp>

namespace common {

connection::connection(asio::ip::tcp::socket &&sock)
    : m_sock{std::move(sock)},
      m_port{m_sock.remote_endpoint().port()},
      m_ip{m_sock.remote_endpoint().address().to_string()},
      m_strand{asio::make_strand(m_sock.get_executor())},
      m_heat_timer{std::make_unique<asio::steady_timer>(m_strand)} {
}

auto connection::start(std::function<asio::awaitable<void>(std::shared_ptr<proto_frame>, std::shared_ptr<connection>)> on_recv_request) -> void {
  m_on_recv_request = on_recv_request;
  auto self = shared_from_this();
  asio::co_spawn(m_strand, [self] { return self->start_recv(); }, exception_handle);
  asio::co_spawn(m_strand, [self] { return self->start_heart(); }, exception_handle);
}

auto connection::close() -> asio::awaitable<void> {
  if (m_closed) {
    co_return;
  }
  m_closed = true;
  for (auto &_ : m_response_frames) {
    _.second.second->cancel();
  }
  m_heat_timer->cancel();
  m_sock.close();
  co_await m_on_recv_request(nullptr, shared_from_this());
  for (auto on_close : m_on_close_works) {
    on_close();
  }
}

auto connection::recv_response(uint16_t id) -> asio::awaitable<std::shared_ptr<proto_frame>> {
  co_await asio::post(m_strand, asio::use_awaitable);
  auto &[frame, waiter] = m_response_frames[id];
  if (frame == nullptr) {
    co_await waiter->async_wait(asio::as_tuple(asio::use_awaitable));
    if (m_closed) {
      co_return nullptr;
    }
  }
  auto ret = frame;
  m_response_frames.erase(id);
  co_return ret;
}

auto connection::send_request(proto_frame *frame, std::source_location loc) -> asio::awaitable<std::optional<uint16_t>> {
  co_await asio::post(m_strand, asio::use_awaitable);
  if (m_closed) {
    co_return std::nullopt;
  }

  frame->magic = FRAME_MAGIC;
  frame->id = m_request_frame_id++;
  frame->type = frame_type::request;
  m_response_frames[frame->id] = {nullptr, std::make_unique<asio::steady_timer>(m_strand, std::chrono::years{1})}; // !!waiter 初始化放在 write 之前，防止收到了 response 但还没有初始化 waiter

  if (co_await send_frame(frame, loc)) {
    co_return frame->id;
  }
  co_return std::nullopt;
}

auto connection::send_request(proto_frame frame, std::source_location loc) -> asio::awaitable<std::optional<uint16_t>> {
  co_return co_await send_request(&frame, loc);
}

auto connection::send_request_without_data(proto_frame frame, std::source_location loc) -> asio::awaitable<std::optional<uint16_t>> {
  co_await asio::post(m_strand, asio::use_awaitable);
  if (m_closed) {
    co_return std::nullopt;
  }

  frame.magic = FRAME_MAGIC;
  frame.id = m_request_frame_id++;
  frame.type = frame_type::request;
  m_response_frames[frame.id] = {nullptr, std::make_unique<asio::steady_timer>(m_strand, std::chrono::days{365})};
  trans_frame_to_net(&frame);

  auto ec = asio::error_code{};
  auto n = asio::write(m_sock, asio::const_buffer(&frame, sizeof(proto_frame)), ec);
  trans_frame_to_host(&frame);

  if (ec || n != sizeof(proto_frame)) {
    LOG_ERROR("[{}:{}] send {} to {} failed, {}", loc.file_name(), loc.line(), proto_frame_to_string(frame), address(), ec.message());
    co_await close();
    co_return std::nullopt;
  }

  LOG_DEBUG("[{}:{}] send {} to {}", loc.file_name(), loc.line(), proto_frame_to_string(frame), address());
  co_return frame.id;
}

auto connection::send_response(proto_frame *frame, const proto_frame &req_frame, std::source_location loc) -> asio::awaitable<bool> {
  co_await asio::post(m_strand, asio::use_awaitable);
  if (m_closed) {
    co_return false;
  }

  frame->id = req_frame.id;
  frame->type = frame_type::response;
  frame->cmd = req_frame.cmd;

  co_return co_await send_frame(frame, loc);
}

auto connection::send_response(proto_frame frame, const proto_frame &req_frame, std::source_location loc) -> asio::awaitable<bool> {
  co_return co_await send_response(&frame, req_frame, loc);
}

auto connection::send_response(const proto_frame &req_frame, std::source_location loc) -> asio::awaitable<bool> {
  co_return co_await send_response({.stat = 0}, req_frame, loc);
}

auto connection::send_response_without_data(proto_frame frame, const proto_frame &req_frame, std::source_location loc) -> asio::awaitable<bool> {
  frame.magic = FRAME_MAGIC;
  frame.id = req_frame.id;
  frame.type = frame_type::response;
  frame.cmd = req_frame.cmd;
  trans_frame_to_net(&frame);

  auto ec = asio::error_code{};
  auto n = asio::write(m_sock, asio::const_buffer(&frame, sizeof(proto_frame)));
  trans_frame_to_host(&frame);

  if (ec || n != sizeof(proto_frame)) {
    LOG_ERROR("[{}:{}] send {} to {} failed, {}", loc.file_name(), loc.line(), proto_frame_to_string(frame), address(), ec.message());
    co_await close();
    co_return false;
  }

  LOG_DEBUG("[{}:{}] send {} to {}", loc.file_name(), loc.line(), proto_frame_to_string(frame), address());
  co_return true;
}

auto connection::send_request_and_wait_response(proto_frame *frame, std::source_location loc) -> asio::awaitable<std::shared_ptr<proto_frame>> {
  co_await asio::post(m_strand, asio::use_awaitable);
  if (m_closed) {
    co_return nullptr;
  }

  auto id = co_await send_request(frame, loc);
  if (!id) {
    co_return nullptr;
  }

  co_return co_await recv_response(id.value());
}

auto connection::send_request_and_wait_response(proto_frame frame, std::source_location loc) -> asio::awaitable<std::shared_ptr<proto_frame>> {
  co_return co_await send_request_and_wait_response(&frame, loc);
}

auto connection::add_exetension_work(std::function<asio::awaitable<void>(std::shared_ptr<connection>)> work, std::function<void()> on_close) -> void {
  asio::co_spawn(m_strand, work(shared_from_this()), exception_handle);
  m_on_close_works.emplace_back(on_close);
}

auto connection::connect_to(std::string_view ip, uint16_t port) -> asio::awaitable<std::shared_ptr<connection>> {
  auto sock = asio::ip::tcp::socket{co_await asio::this_coro::executor};

  auto [ec] = co_await sock.async_connect(asio::ip::tcp::endpoint{asio::ip::make_address(ip), port}, asio::as_tuple(asio::use_awaitable));
  if (ec) {
    LOG_ERROR("connect to {}:{} failed {}", ip, port, ec.message());
    co_return nullptr;
  }

  /* 建立心跳 */
  char request_recved[sizeof(proto_frame) + sizeof(xx_heart_establish_request)];
  auto [ec_, n] = co_await asio::async_read(sock, asio::mutable_buffer(&request_recved, sizeof(request_recved)), asio::as_tuple(asio::use_awaitable));
  if (n != sizeof(request_recved)) {
    LOG_ERROR("recv establish heart failed {}", ec_.message());
    sock.close();
    co_return nullptr;
  }

  auto req_header = (proto_frame *)request_recved;
  trans_frame_to_host(req_header);
  if (req_header->magic != FRAME_MAGIC || req_header->cmd != proto_cmd::xx_heart_establish) {
    LOG_ERROR(std::format("recv establish heart failed, invalid frame"));
    sock.close();
    co_return nullptr;
  }

  auto res_header = proto_frame{.cmd = proto_cmd::xx_heart_establish};
  trans_frame_to_host(&res_header);
  std::tie(ec, n) = co_await asio::async_write(sock, asio::const_buffer(&res_header, sizeof(res_header)), asio::as_tuple(asio::use_awaitable));
  if (n != sizeof(proto_frame)) {
    LOG_ERROR(std::format("send establish heart failed {}", ec.message()));
    sock.close();
    co_return nullptr;
  }

  auto req_data = (xx_heart_establish_request *)req_header->data;
  auto conn = std::make_shared<connection>(std::move(sock));
  conn->m_heart_timeout = ntohl(req_data->timeout);
  conn->m_heart_interval = ntohl(req_data->interval);
  co_return conn;
}

auto connection::start_heart() -> asio::awaitable<void> {
  auto frame = proto_frame{.cmd = proto_cmd::xx_heart_ping};
  trans_frame_to_net(&frame);
  while (!m_closed) {
    m_heat_timer->expires_after(std::chrono::milliseconds{m_heart_interval});
    co_await m_heat_timer->async_wait(asio::as_tuple(asio::use_awaitable));

    auto [ec, n] = co_await asio::async_write(m_sock, asio::const_buffer(&frame, sizeof(frame)), asio::as_tuple(asio::use_awaitable));
    if (ec || n != sizeof(frame)) {
      co_await close();
      co_return;
    }
  }
}

auto connection::start_recv() -> asio::awaitable<void> {
  auto frame_header = proto_frame{};
  while (!m_closed) {
    /* 超时异步读取 */
    auto timer = asio::steady_timer{m_strand, std::chrono::milliseconds{m_heart_timeout}};
    timer.async_wait([this](const asio::error_code &ec) {
      if (!ec) {
        m_sock.cancel();
        LOG_CRITICAL(std::format("recv frame timeout"));
      }
    });

    auto [ec, n] = co_await asio::async_read(m_sock, asio::mutable_buffer(&frame_header, sizeof(proto_frame)), asio::as_tuple(asio::use_awaitable));
    if (n != sizeof(proto_frame)) {
      timer.cancel();
      co_await close();
      co_return;
    }
    trans_frame_to_host(&frame_header);

    /* 校验 magic */
    if (frame_header.magic != FRAME_MAGIC || (frame_header.type != frame_type::request && frame_header.type != frame_type::response)) {
      LOG_ERROR(std::format("recv invalid magic {:#x}", frame_header.magic));
      co_await close();
      co_return;
    }

    // /* 忽略 heartbeat */
    if (frame_header.cmd == proto_cmd::xx_heart_ping) {
      continue;
    }

    /* 读取 payload */
    // LOG_DEBUG("recv frame header {}", proto_frame_to_string(frame_header));
    auto frame = std::shared_ptr<proto_frame>{(proto_frame *)malloc(sizeof(proto_frame) + frame_header.data_len), free};
    if (frame == nullptr) {
      LOG_ERROR(std::format("alloc memory failed, requested size is {}MB", frame_header.data_len / 1024 / 1024));
      co_return;
    }

    std::tie(ec, n) = co_await asio::async_read(m_sock, asio::mutable_buffer(frame.get()->data, frame_header.data_len), asio::as_tuple(asio::use_awaitable));
    if (n != frame_header.data_len) {
      LOG_ERROR(std::format("recv payload failed {}", ec.message()));
      co_await close();
      co_return;
    }
    *frame = frame_header;

    LOG_DEBUG("recv {} from {}", proto_frame_to_string(*frame), address());
    if (frame->type == frame_type::request) {
      asio::co_spawn(m_strand, m_on_recv_request(frame, shared_from_this()), exception_handle);
    } else if (frame->type == frame_type::response) {
      auto &[res_frame, waiter] = m_response_frames[frame->id];
      res_frame = frame;
      if (waiter != nullptr) {
        waiter->cancel();
      } else {
        LOG_CRITICAL("waiter is nullptr");
        exit(-1);
      }
    }
  }
}

auto connection::send_frame(proto_frame *frame, std::source_location loc) -> asio::awaitable<bool> {
  auto message_len = sizeof(proto_frame) + frame->data_len;
  trans_frame_to_net(frame);
  auto ec = asio::error_code{};
  auto n = asio::write(m_sock, asio::const_buffer(frame, message_len), ec);
  trans_frame_to_host(frame);

  if (ec || n != message_len) {
    LOG_ERROR("[{}:{}] send {} to {} failed, {}", loc.file_name(), loc.line(), proto_frame_to_string(*frame), address(), ec.message());
    co_await close();
    co_return false;
  }

  LOG_DEBUG("[{}:{}] send {} to {} suc", loc.file_name(), loc.line(), proto_frame_to_string(*frame), address());
  co_return true;
}

} // namespace common