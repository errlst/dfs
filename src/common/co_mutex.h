#pragma once
#include <asio.hpp>
#include <queue>

class co_mutex_t {
  public:
    co_mutex_t(asio::any_io_executor io) : m_strand{io} {}

    auto lock() -> asio::awaitable<void>;

    auto unlock() -> asio::awaitable<void>;

  private:
    asio::strand<asio::any_io_executor> m_strand; // 保证线程安全
    bool m_locked = false;                        // 锁状态
    std::queue<std::function<void()>> m_waiters;  // 等待队列
};