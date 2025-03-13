#pragma once
#include "../common/log.h"
#include "./protocol.h"
#include <any>
#include <asio.hpp>
#include <map>
#include <source_location>

namespace common {

class connection : public std::enable_shared_from_this<connection> {
  friend class acceptor;

public:
  connection(asio::ip::tcp::socket &&sock);

  ~connection() = default;

  /**
   * @brief 开始处理接受数据和心跳
   *
   */
  auto start(std::function<asio::awaitable<void>(std::shared_ptr<proto_frame>, std::shared_ptr<connection>)> on_recv_request) -> void;

  /**
   * @brief 接受响应
   *
   */
  auto recv_response(uint16_t id) -> asio::awaitable<std::shared_ptr<proto_frame>>;

  /**
   * @brief 发送请求，frame 只需要设置 cmd 和 data_len 字段。保证发送前后的 frame 一致
   *
   * @return 成功返回对应的 id
   */
  auto send_request(proto_frame *frame, std::source_location loc = std::source_location::current()) -> asio::awaitable<std::optional<uint16_t>>;
  auto send_request(proto_frame frame, std::source_location loc = std::source_location::current()) -> asio::awaitable<std::optional<uint16_t>>;

  /**
   * @brief 发送请求，用于零拷贝优化
   *
   */
  auto send_request_without_data(proto_frame frame, std::source_location loc = std::source_location::current()) -> asio::awaitable<std::optional<uint16_t>>;

  /**
   * @brief 发送响应，frame 只需要设置 sta 和 data_len。保证发送前后的 frame 一致。
   *
   */
  auto send_response(proto_frame *frame, std::shared_ptr<proto_frame> req_frame, std::source_location loc = std::source_location::current()) -> asio::awaitable<bool>;
  auto send_response(proto_frame frame, std::shared_ptr<proto_frame> req_frame, std::source_location loc = std::source_location::current()) -> asio::awaitable<bool>;

  /**
   * @brief 发送请求并等待响应
   *
   */
  auto send_request_and_wait_response(proto_frame *frame, std::source_location loc = std::source_location::current()) -> asio::awaitable<std::shared_ptr<proto_frame>>;
  auto send_request_and_wait_response(proto_frame frame, std::source_location loc = std::source_location::current()) -> asio::awaitable<std::shared_ptr<proto_frame>>;

  /**
   * @brief 在 connection 的 strand 中增加任务
   *
   * @param on_close 连接断开后的任务处理函数
   */
  auto add_exetension_work(std::function<asio::awaitable<void>(std::shared_ptr<connection>)> work, std::function<void()> on_close) -> void;

  /**
   * @brief 获取 ip
   *
   */
  auto ip() -> std::string { return m_ip; }

  /**
   * @brief 获取端口
   *
   */
  auto port() -> uint16_t { return m_port; }

  /**
   * @brief <ip>:<port>
   *
   * @return std::string
   */
  auto address() -> std::string { return std::format("{}:{}", ip(), port()); }

  /**
   * @brief 设置用户数据
   *
   */
  template <typename T>
  auto set_data(uint64_t key, T value) -> void { m_datas[key] = std::move(value); }

  /**
   * @brief 获取用户数据
   *
   */
  template <typename T>
  auto get_data(uint64_t key) -> std::optional<T> {
    auto it = m_datas.find(key);
    if (it == m_datas.end()) {
      return std::nullopt;
    }
    return std::any_cast<T>(it->second);
  }

  /**
   * @brief 判断用户数据是否存在
   *
   */
  auto has_data(uint64_t key) -> bool { return m_datas.contains(key); }

  /**
   * @brief 删除用户数据
   *
   */
  auto del_data(uint64_t key) -> void { m_datas.erase(key); }

  /**
   * @brief 关闭连接
   *
   */
  auto close() -> asio::awaitable<void>;

  /**
   * @brief 获取 native socket
   *
   */
  auto native_socket() -> int { return m_sock.native_handle(); }

  /* 如果成功，返回已经建立心跳的 connection，但没有 start */

  /**
   * @brief 连接到对端，且建立心跳，但未 start
   *
   */
  static auto connect_to(std::string_view ip, uint16_t port) -> asio::awaitable<std::shared_ptr<connection>>;

private:
  /**
   * @brief 开启心跳
   *
   */
  auto start_heart() -> asio::awaitable<void>;

  /**
   * @brief 开始接受消息
   *
   */
  auto start_recv() -> asio::awaitable<void>;

private:
  asio::ip::tcp::socket m_sock;

  /* 地址 */
  uint16_t m_port;
  std::string m_ip;

  /* 使用 strand 保证即使一个 connection 运行在多个线程中，同一时刻也只会有一个 connection 在工作，实现无锁线程安全 */
  asio::strand<asio::any_io_executor> m_strand;

  /* 请求 frame id */
  std::atomic_uint16_t m_request_frame_id = 0;

  /* 心跳 */
  std::unique_ptr<asio::steady_timer> m_heat_timer;
  uint32_t m_heart_timeout = -1;
  uint32_t m_heart_interval = -1;

  /* 关闭连接 */
  std::vector<std::function<void()>> m_on_close_works;
  bool m_closed = false;

  /* 用户定义数据 */
  std::map<uint64_t, std::any> m_datas;

  /* 响应 frame 缓冲 */
  std::map<uint16_t, std::pair<std::shared_ptr<proto_frame>, std::unique_ptr<asio::steady_timer>>> m_response_frames;

  /* 收到 request 后的回调 */
  std::function<asio::awaitable<void>(std::shared_ptr<proto_frame>, std::shared_ptr<connection>)> m_on_recv_request;
};
} // namespace common
