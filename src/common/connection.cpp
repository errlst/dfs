#include "connection.h"
#include "../common/util.h"
#include "timeout_token.h"

connection_t::connection_t(asio::ip::tcp::socket &&sock, std::shared_ptr<log_t> log)
    : m_sock{std::move(sock)},
      m_log{log},
      m_ip{m_sock.remote_endpoint().address().to_string()},
      m_port{m_sock.remote_endpoint().port()},
      m_strand(asio::make_strand(sock.get_executor())) {
}

connection_t::~connection_t() {
  std::println("connect dstructed");
  close();
}

auto connection_t::start(asio::io_context &io) -> void {
  m_work_thread = std::this_thread::get_id();
  m_log->log_warn(std::format("work in thread {}", *(uint64_t *)(&m_work_thread)));
  asio::co_spawn(m_strand, start_recv(), asio::detached);
  asio::co_spawn(m_strand, start_heart(), asio::detached);
}

auto connection_t::recv_req_frame() -> asio::awaitable<std::shared_ptr<proto_frame_t>> {
  if (std::this_thread::get_id() != m_work_thread) {
    m_log->log_fatal("work in diff thread");
    abort();
  }
  if (m_req_frames_waiter == nullptr) {
    m_req_frames_waiter = std::make_unique<asio::steady_timer>(co_await asio::this_coro::executor);
  }
  while (m_req_frames.empty()) {
    m_req_frames_waiter->expires_after(std::chrono::days{365});
    co_await m_req_frames_waiter->async_wait(asio::as_tuple(asio::use_awaitable));
    if (m_closed) {
      co_return nullptr;
    }
  }

  auto frame = m_req_frames.front();
  m_req_frames.pop();
  co_return frame;
}

auto connection_t::send_res_frame(std::shared_ptr<proto_frame_t> frame, uint16_t id) -> asio::awaitable<bool> {
  m_log->log_debug(std::format("send response {}", frame->cmd));
  if (std::this_thread::get_id() != m_work_thread) {
    m_log->log_fatal("work in diff thread");
    abort();
  }
  frame->id = id;
  co_return co_await do_send(frame.get(), sizeof(proto_frame_t) + frame->data_len);
}

auto connection_t::send_res_frame(proto_frame_t frame, uint16_t id) -> asio::awaitable<bool> {
  m_log->log_debug(std::format("send response {}", frame.cmd));
  if (std::this_thread::get_id() != m_work_thread) {
    m_log->log_fatal("work in diff thread");
    abort();
  }
  frame.id = id;
  co_return co_await do_send(&frame, sizeof(proto_frame_t) + frame.data_len);
}

auto connection_t::recv_res_frame(uint16_t frame_id) -> asio::awaitable<std::shared_ptr<proto_frame_t>> {
  if (std::this_thread::get_id() != m_work_thread) {
    m_log->log_fatal("work in diff thread");
    abort();
  }
  if (m_res_frame_waiters[frame_id] == nullptr) {
    m_res_frame_waiters[frame_id] = std::make_unique<asio::steady_timer>(co_await asio::this_coro::executor);
  }
  while (m_res_frames[frame_id] == nullptr) {
    m_res_frame_waiters[frame_id]->expires_after(std::chrono::days(365));
    co_await m_res_frame_waiters[frame_id]->async_wait(asio::as_tuple(asio::use_awaitable));
    if (m_closed) {
      m_log->log_warn("recv res frame closed");
      co_return nullptr;
    }
  }

  auto frame = m_res_frames[frame_id];
  m_res_frames[frame_id] = nullptr;
  // m_log->log_debug(std::format("recv frame {} {}", (void *)frame.get(), frame_id));
  co_return frame;
}

auto connection_t::send_req_frame(std::shared_ptr<proto_frame_t> frame) -> asio::awaitable<std::optional<uint16_t>> {
  if (std::this_thread::get_id() != m_work_thread) {
    m_log->log_fatal("work in diff thread");
    abort();
  }
  if (m_closed) {
    co_return false;
  }
  // m_log->log_debug(std::format("send request {}", frame->cmd));
  auto id = m_frame_id++;
  frame->id = id;
  if (co_await do_send(frame.get(), sizeof(proto_frame_t) + frame->data_len)) {
    co_return id;
  }
  co_return std::nullopt;
}

auto connection_t::send_req_frame(proto_frame_t *frame) -> asio::awaitable<std::optional<uint16_t>> {
  if (std::this_thread::get_id() != m_work_thread) {
    m_log->log_fatal("work in diff thread");
    abort();
  }
  if (m_closed) {
    co_return false;
  }
  m_log->log_debug(std::format("send request {}", frame->cmd));
  auto id = m_frame_id++;
  frame->id = id;
  if (co_await do_send(frame, sizeof(proto_frame_t) + frame->data_len)) {
    co_return id;
  }
  co_return std::nullopt;
}

auto connection_t::send_req_frame(proto_frame_t frame) -> asio::awaitable<std::optional<uint16_t>> {
  if (std::this_thread::get_id() != m_work_thread) {
    m_log->log_fatal("work in diff thread");
    abort();
  }
  if (m_closed) {
    co_return false;
  }
  m_log->log_debug(std::format("send request {}", frame.cmd));
  auto id = m_frame_id++;
  frame.id = id;
  if (co_await do_send(&frame, sizeof(proto_frame_t) + frame.data_len)) {
    co_return id;
  }
  co_return std::nullopt;
}

auto connection_t::do_send(void *data, uint64_t len) -> asio::awaitable<bool> {
  if (std::this_thread::get_id() != m_work_thread) {
    m_log->log_fatal("work in diff thread");
    abort();
  }
  if (auto [ec, n] = co_await asio::async_write(m_sock, asio::const_buffer(data, len), asio::as_tuple(asio::use_awaitable));
      ec || n != len) {
    m_log->log_error(std::format("send error from {}, errmsg {} {}/{}", to_string(), ec.message(), n, len));
    close();
    co_return false;
  }
  // m_log->log_debug(std::format("send frame to {} cmd: {} len: {} data_len: {}", to_string(), (uint16_t)((proto_frame_t *)data)->cmd, len, (uint32_t)((proto_frame_t *)data)->data_len));
  co_return true;
}

auto connection_t::close() -> void {
  if (m_closed) {
    return;
  }
  if (std::this_thread::get_id() != m_work_thread) {
    auto id = std::this_thread::get_id();
    m_log->log_fatal(std::format("work in diff thread {} {}", *(uint64_t *)&id, *(uint64_t *)(&m_work_thread)));
    abort();
  }
  // m_log->log_debug("call close()");

  m_closed = true;
  m_sock.close();
  // if (m_req_frame_waiter) {
  //   m_req_frame_waiter->cancel();
  // }
  for (auto i = 0; i < m_res_frame_waiters.size(); ++i) {
    if (m_res_frame_waiters[i]) {
      m_res_frame_waiters[i]->cancel();
    }
  }
  if (m_req_frames_waiter) {
    m_req_frames_waiter->cancel();
  }
  // while (!m_req_frame_waiters.empty()) {
  //   auto &waiter = m_req_frame_waiters.front();
  //   waiter.cancel();
  //   m_req_frame_waiters.pop();
  // }

  // m_req_frames_not_empty.notify_all();
  // for (auto &cv : m_res_frame_valid) {
  //     cv.notify_all();
  // }
}

auto connection_t::start_heart() -> asio::awaitable<void> {

  static auto heartbeat_frame = proto_frame_t{.cmd = static_cast<uint8_t>(proto_cmd_e::a_heart), .data_len = 0};
  static auto heartbeat_buffer = asio::const_buffer{&heartbeat_frame, sizeof(heartbeat_frame)};
  auto timer = asio::steady_timer{co_await asio::this_coro::executor};
  while (true) {
    co_await co_sleep_for(std::chrono::milliseconds{m_heart_interval});
    if (m_closed) {
      // m_log->log_debug("heart beat closed");
      co_return;
    }

    auto [ec, n] = co_await m_sock.async_write_some(heartbeat_buffer, asio::as_tuple(asio::use_awaitable));
    if (ec || n != sizeof(heartbeat_frame)) {
      auto errmsg = ec ? ec.message() : std::format("invliad send size: {}", n);
      m_log->log_error(std::format("send error from {}, errmsg {}", to_string(), errmsg));
      close();
      co_return;
    }
    // m_log->log_debug(std::format("send heartbeat to {}", to_string()));
  }
}

auto connection_t::start_recv() -> asio::awaitable<void> {
  auto header_buf = proto_frame_t{};
  auto read_token = asio::as_tuple(asio::use_awaitable);
  auto read_timeout_token = timeout_token(read_token, std::chrono::milliseconds{m_heart_timeout});

  while (!m_closed) {
    // auto id = std::this_thread::get_id();
    // m_log->log_debug(std::format("{} work in thread {}", to_string(), *(uint64_t *)&id));

    if (std::this_thread::get_id() != m_work_thread) {
      m_log->log_fatal("work in diff thread");
      abort();
    }

    /* 只在此处进行超时判断 */
    if (auto [ec, n] = co_await m_sock.async_read_some(asio::mutable_buffer{&header_buf, sizeof(header_buf)}, read_timeout_token);
        ec || n != sizeof(header_buf)) {
      auto errmsg = ec ? ec.message() : n == 0 ? "heartbeat timeout"
                                               : "invliad send size";
      std::println("{}", (void *)m_log.get());
      m_log->log_error(std::format("recv error from {} ec={} n={}", to_string(), ec.message(), n));
      close();
      co_return;
    }
    // m_log->log_debug(std::format("recv frame from {} cmd: {} data_len: {}", to_string(), (uint16_t)header_buf.cmd, (uint32_t)header_buf.data_len));

    /* 校验 magic */
    if (header_buf.magic != FRAME_MAGIC) {
      m_log->log_error(std::format("invalid magic from {} {:#x}", to_string(), header_buf.magic));
      close();
      co_return;
    }

    /* 忽略 heartbeat */
    if (header_buf.cmd == (uint8_t)proto_cmd_e::a_heart) {
      // m_log->log_debug(std::format("recv heart from {}", to_string()));
      continue;
    }

    // m_log->log_debug(std::format("recv cmd {}", header_buf.cmd));

    /* 读取 payload */
    auto frame = std::shared_ptr<proto_frame_t>{(proto_frame_t *)malloc(sizeof(proto_frame_t) + header_buf.data_len), [](auto p) { free(p); }};
    if (frame == nullptr) {
      m_log->log_error(std::format("alloc memory failed, requested size is {}MB", header_buf.data_len / 1024 / 1024));
      co_return;
    }
    memcpy(frame.get(), &header_buf, sizeof(proto_frame_t));
    if (auto [ec, n] = co_await asio::async_read(m_sock, asio::mutable_buffer{reinterpret_cast<char *>(frame.get()) + sizeof(proto_frame_t), header_buf.data_len}, read_token);
        ec || n != header_buf.data_len) {
      auto errmsg = n != header_buf.data_len ? std::format("invliad read size {}", n) : ec.message();
      m_log->log_error(std::format("recv error from {}  {}", to_string(), errmsg));
      close();
      co_return;
    }
    // m_log->log_debug(std::format("recv payload len = {}", (uint32_t)header_buf.data_len));

    /* 处理 request frame 和 response frame */
    if (frame->id >= m_frame_id) {
      m_frame_id = frame->id + 1;
      slove_req_frame(frame);
    } else {
      slove_res_frame(frame);
    }
  }
}

auto connection_t::slove_req_frame(std::shared_ptr<proto_frame_t> frame) -> void {
  m_log->log_debug(std::format("recv request {}", frame->cmd));
  // auto lock = std::unique_lock{m_req_frames_mut};
  m_req_frames.push(frame);
  if (m_req_frames_waiter) {
    m_req_frames_waiter->cancel();
  }
  // if (!m_req_frames.empty()) {
  //   m_req_frame_waiters.front().cancel();
  //   m_req_frame_waiters.pop();
  // }
  // m_req_frames_not_empty.notify_one();
}

auto connection_t::slove_res_frame(std::shared_ptr<proto_frame_t> frame) -> void {
  // m_log->log_debug(std::format("recv response {}", frame->id));

  // auto lock = std::unique_lock{m_res_frame_muts[frame->id]};
  // if (m_res_frames[frame->id] != nullptr) {
  //     close();
  //     return;
  // }
  m_res_frames[frame->id] = frame;
  if (m_res_frame_waiters[frame->id]) {
    m_res_frame_waiters[frame->id]->cancel();
  }
  // m_res_frame_valid[frame->id].notify_one();
}

auto connection_t::connect_to(asio::io_context &io, std::string_view ip, uint16_t port, std::shared_ptr<log_t> log) -> asio::awaitable<std::shared_ptr<connection_t>> {
  /* connect to remote */
  auto sock = asio::ip::tcp::socket{io};
  if (auto [ec] = co_await sock.async_connect(asio::ip::tcp::endpoint{asio::ip::make_address(ip), port}, asio::as_tuple(asio::use_awaitable)); ec) {
    co_return nullptr;
  }

  /* wait for heart establish */
  char req_data[sizeof(proto_frame_t) + 8];
  auto [ec, n] = co_await sock.async_read_some(asio::mutable_buffer(&req_data, sizeof(req_data)), asio::as_tuple(asio::use_awaitable));
  if (ec || n != sizeof(req_data)) {
    auto res_frame = proto_frame_t{.cmd = (uint8_t)proto_cmd_e::a_establish_heart, .stat = 1};
    co_await sock.async_write_some(asio::const_buffer(&res_frame, sizeof(res_frame)), asio::use_awaitable);
    sock.close();
    co_return nullptr;
  }

  /* check frame valid */
  auto req_frame = (proto_frame_t *)req_data;
  if (req_frame->magic != FRAME_MAGIC || req_frame->cmd != (uint8_t)proto_cmd_e::a_establish_heart || req_frame->stat != 0) {
    auto res_frame = proto_frame_t{.cmd = (uint8_t)proto_cmd_e::a_establish_heart, .stat = 2};
    co_await sock.async_write_some(asio::const_buffer(&res_frame, sizeof(res_frame)), asio::use_awaitable);
    sock.close();
    co_return nullptr;
  }

  /* send response */
  auto res_frame = proto_frame_t{.cmd = (uint8_t)proto_cmd_e::a_establish_heart, .stat = 0};
  auto [ec2, n2] = co_await sock.async_write_some(asio::const_buffer(&res_frame, sizeof(res_frame)), asio::as_tuple(asio::use_awaitable));
  if (ec2 || n2 != sizeof(res_frame)) {
    sock.close();
    co_return nullptr;
  }

  auto conn = std::make_shared<connection_t>(std::move(sock), log);
  conn->m_heart_timeout = *(uint32_t *)req_frame->data;
  conn->m_heart_interval = *(uint32_t *)(req_frame->data + 4);
  conn->start(io);
  co_return conn;
}
