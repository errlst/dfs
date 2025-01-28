#pragma once
#include "./log.h"
#include "./protocol.h"
#include <any>
#include <asio.hpp>
#include <map>
#include <queue>

class connection_t {
    friend class acceptor_t;

  public:
    connection_t(asio::ip::tcp::socket &&sock, std::shared_ptr<log_t> log);

    auto start() -> asio::awaitable<void>;

    /*
        接受 request frame
        返回 nullptr 表示断开连接
    */
    auto recv_req_frame() -> asio::awaitable<std::shared_ptr<proto_frame_t>>;

    /*
        发送 response frame，frame_id 由调用者设置
        返回 false 表示断开连接
    */
    auto send_res_frame(std::shared_ptr<proto_frame_t>) -> asio::awaitable<bool>;
    auto send_res_frame(proto_frame_t) -> asio::awaitable<bool>;

    /*
        接受 response frame
        返回 nullptr 表示断开连接
    */
    auto recv_res_frame(uint8_t id) -> asio::awaitable<std::shared_ptr<proto_frame_t>>;

    /*
        发送 request frame，frame_id 由 connection 设置
        返回 nullopt 表示断开连接
    */
    auto send_req_frame(std::shared_ptr<proto_frame_t>) -> asio::awaitable<std::optional<uint8_t>>;
    auto send_req_frame(proto_frame_t) -> asio::awaitable<std::optional<uint8_t>>;

    // /* 读取协议帧，如果返回空，则表示连接已经断开 */
    // auto recv_frame() -> asio::awaitable<std::shared_ptr<proto_frame_t>>;

    /* ip:port */
    auto to_string() -> std::string { return m_ip + ":" + std::to_string(m_port); }

    auto ip() -> std::string { return m_ip; }

    auto port() -> uint16_t { return m_port; }

    auto close() -> void;

    template <typename T>
    auto set_data(uint64_t key, T &&value) -> void {
        m_any_data[key] = std::forward<T>(value);
    }

    template <typename T>
    auto get_data(uint64_t key) -> std::optional<T> {
        auto it = m_any_data.find(key);
        if (it == m_any_data.end()) {
            return std::nullopt;
        }
        if (it->second.type() != typeid(T)) {
            return std::nullopt;
        }
        return std::any_cast<T>(it->second);
    }

    /*
        如果成功，返回已经建立心跳连接的 connection，且已经开启 start
    */
    static auto connect_to(std::string_view ip, uint16_t port, std::shared_ptr<log_t> log) -> asio::awaitable<std::shared_ptr<connection_t>>;

  private:
    /* 心跳包看门狗 */
    auto start_heart() -> asio::awaitable<void>;

    /* 收发消息，根据 frame_id 将 frame 处理为 req 或 res */
    auto start_recv() -> asio::awaitable<void>;

    auto slove_req_frame(std::shared_ptr<proto_frame_t>) -> asio::awaitable<void>;

    auto slove_res_frame(std::shared_ptr<proto_frame_t>) -> asio::awaitable<void>;

    auto do_send(void *data, uint64_t len) -> asio::awaitable<bool>;

  private:
    asio::ip::tcp::socket m_sock;
    std::shared_ptr<log_t> m_log;
    std::string m_ip;
    uint16_t m_port;
    std::atomic_bool m_closed = false;
    std::atomic_uint8_t m_frame_id = 0;

    bool m_heart_started = false;
    uint32_t m_heart_timeout = -1;
    uint32_t m_heart_interval = -1;

    /* 当 m_req_frames 为空时，recv_req_frame 通过 timer 等待有新的 req_frame 加入  */
    std::queue<asio::steady_timer> m_req_frame_waiters;
    std::queue<std::shared_ptr<proto_frame_t>> m_req_frames;

    /* 同上*/
    std::shared_ptr<asio::steady_timer> m_res_frame_waiter;
    std::array<std::shared_ptr<proto_frame_t>, UINT8_MAX + 1> m_res_frames;

    std::map<uint64_t, std::any> m_any_data;
};