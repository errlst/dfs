#pragma once
#include "./log.h"
#include "./protocol.h"
#include <any>
#include <asio.hpp>
#include <map>

namespace common {
class connection : public std::enable_shared_from_this<connection> {
  friend class acceptor;

public:
  connection(asio::ip::tcp::socket &&sock);

  ~connection();

  auto start(std::function<asio::awaitable<void>(std::shared_ptr<proto_frame>, std::shared_ptr<connection>)> on_recv_request) -> void;

  auto recv_response(uint16_t id) -> asio::awaitable<std::shared_ptr<proto_frame>>;

  /* frame 只需要设置 cmd 和 data_len */
  auto send_request(proto_frame *frame) -> asio::awaitable<std::optional<uint16_t>>;
  auto send_request(proto_frame frame) -> asio::awaitable<std::optional<uint16_t>>;

  /* frame 只需要设置 stat 和 data_len */
  auto send_response(proto_frame *frame, std::shared_ptr<proto_frame> req_frame) -> asio::awaitable<bool>;
  auto send_response(proto_frame frame, std::shared_ptr<proto_frame> req_frame) -> asio::awaitable<bool>;

  /* 在 strand 中增加任务 */
  auto add_exetension_work(std::function<asio::awaitable<void>(std::shared_ptr<connection>)> work, std::function<void()> on_close) -> void;

  auto ip() -> std::string;

  auto port() -> uint16_t;

  template <typename T>
  auto set_data(uint64_t key, T value) -> void { datas_[key] = std::move(value); }

  template <typename T>
  auto get_data(uint64_t key) -> std::optional<T> {
    auto it = datas_.find(key);
    if (it == datas_.end()) {
      return std::nullopt;
    }
    if (it->second.type() != typeid(T)) {
      LOG_ERROR(std::format("need type is {} but stored type is {}", typeid(T).name(), it->second.type().name()));
      abort();
    }
    return std::any_cast<T>(it->second);
  }

  auto has_data(uint64_t key) -> bool { return datas_.contains(key); }

  auto del_data(uint64_t key) -> void { datas_.erase(key); }

  /* 如果成功，返回已经建立心跳的 connection，但没有 start */
  static auto connect_to(std::string_view ip, uint16_t port) -> asio::awaitable<std::shared_ptr<connection>>;

private:
  auto close() -> asio::awaitable<void>;

  auto start_heart() -> asio::awaitable<void>;

  auto start_recv() -> asio::awaitable<void>;

private:
  asio::ip::tcp::socket sock_;
  asio::strand<asio::any_io_executor> strand_;
  std::atomic_uint16_t request_frame_id_ = 0;
  bool closed_ = false;

  std::shared_ptr<asio::steady_timer> heart_timer_;
  uint32_t h_timeout_ = -1;
  uint32_t h_interval_ = -1;

  std::vector<std::function<void()>> on_close_works_;

  std::map<uint16_t, std::pair<std::shared_ptr<proto_frame>, std::shared_ptr<asio::steady_timer>>> response_frames_;
  std::map<uint64_t, std::any> datas_;
  std::function<asio::awaitable<void>(std::shared_ptr<proto_frame>, std::shared_ptr<connection>)> on_recv_request_;
};
} // namespace common
