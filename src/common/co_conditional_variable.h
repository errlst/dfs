#pragma once
#include <asio.hpp>
#include <queue>

class co_conditional_variable_t {
  public:
    co_conditional_variable_t(const asio::any_io_executor &ex)
        : m_strand{asio::make_strand(ex)} {}

    auto wait() -> asio::awaitable<void>;

    auto notify_one() -> asio::awaitable<void>;

    auto notify_all() -> asio::awaitable<void>;

  private:
    asio::strand<asio::any_io_executor> m_strand; // 确保线程安全
    std::queue<std::function<void()>> m_waiters;  // 等待队列
};