module;

#include "log.h"
#include <any>
#include <asio.hpp>
#include <map>
#include <source_location>
#include <stacktrace>

export module common.connection;

import common.protocol;
import common.exception;
import common.log;

export namespace common {

  class connection;

  using connection_ptr = std::shared_ptr<connection>;

  /**
   * @brief connection 使用同步的方式发送数据，异步的方式接收数据
   *
   * 在异步发送数据时，asio::asyc_write 内部会多次调用 asio::async_write_some，因此如果有多个协程同时进行异步发送，会导致数据错乱
   *
   * 有两种解决方案：
   *
   *      1. 异步 + 缓冲队列。但是对于当前协议格式，对于具有相同 payload 的 frame，也需要构造多个 frame（因为 frame 中包含了 id），且这种方式会导致发送的数据延迟增加。因此不考虑。
   *
   *          也可以尝试将 frame_header 和 payload 分开处理，但是这样会导致发送数据的复杂度增加，且不符合当前的设计。
   *
   *      2. 同步发送数据。
   */
  class connection : public std::enable_shared_from_this<connection> {

  public:
    connection(asio::ip::tcp::socket &&sock,
               uint32_t heart_timeout, uint32_t heart_interval);

    ~connection() = default;

    /**
     * @brief 开始处理接受数据和心跳
     *
     */
    auto start(std::function<asio::awaitable<void>(std::shared_ptr<proto_frame>, connection_ptr)> on_recv_request) -> void;

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
    auto send_request_without_data(proto_frame frame, std::source_location loc = std::source_location::current()) -> asio::awaitable<std::optional<uint16_t>>;

    /**
     * @brief 发送响应，frame 只需要设置 sta 和 data_len。保证发送前后的 frame 一致。
     *
     */
    auto send_response(proto_frame *frame, const proto_frame &req_frame, std::source_location loc = std::source_location::current()) -> asio::awaitable<bool>;
    auto send_response(proto_frame frame, const proto_frame &req_frame, std::source_location loc = std::source_location::current()) -> asio::awaitable<bool>;
    auto send_response(const proto_frame &req_frame, std::source_location loc = std::source_location::current()) -> asio::awaitable<bool>;
    auto send_response_without_data(proto_frame frame, const proto_frame &req_frame, std::source_location loc = std::source_location::current()) -> asio::awaitable<bool>;

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
    auto add_work(std::function<asio::awaitable<void>(connection_ptr)> work,
                  std::function<asio::awaitable<void>(connection_ptr)> on_close) -> void;

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
    auto get_data(uint64_t key) -> std::optional<T> try {
      auto it = m_datas.find(key);
      if (it == m_datas.end()) {
        return std::nullopt;
      }
      return std::any_cast<T>(it->second);
    } catch (const std::bad_any_cast &ec) {
      auto entry = std::stacktrace::current(1, 1).at(0);
      LOG_CRITICAL("[{}:{}] get key {} invalid type {}", entry.source_file(), entry.source_line(), key, typeid(T).name());
      return std::nullopt;
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
    static auto connect_to(std::string_view ip, uint16_t port) -> asio::awaitable<connection_ptr>;

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

    /**
     * @brief 发送帧
     *
     * @param frame 会将 frame_header 和 payload 一起发送，且发送前和发送后都会转换 frame_header 的字节序
     */
    auto send_frame(proto_frame *frame, std::source_location loc) -> asio::awaitable<bool>;

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
    std::vector<std::function<asio::awaitable<void>(connection_ptr)>> m_on_close_works;
    bool m_closed = false;

    /* 用户定义数据 */
    std::map<uint64_t, std::any> m_datas;

    /* 响应 frame 缓冲 */
    std::map<uint16_t, std::pair<std::shared_ptr<proto_frame>, std::unique_ptr<asio::steady_timer>>> m_response_frames;

    /* 收到 request 后的回调 */
    std::function<asio::awaitable<void>(std::shared_ptr<proto_frame>, connection_ptr)> m_on_recv_request;
  };

} // namespace common
