#include "./co_mutex.h"

auto co_mutex_t::lock() -> asio::awaitable<void> {
    co_await asio::post(m_strand, asio::use_awaitable);
    if (m_locked) {
        auto timer = asio::steady_timer{co_await asio::this_coro::executor, std::chrono::hours{24 * 365}};
        m_waiters.emplace([&timer] { timer.cancel(); });
        co_await timer.async_wait(asio::as_tuple(asio::use_awaitable));
    }
    m_locked = true;
}

auto co_mutex_t::unlock() -> asio::awaitable<void> {
    if (!m_waiters.empty()) {
        auto waiter = std::move(m_waiters.front());
        m_waiters.pop();
        waiter();
    } else {
        m_locked = false;
    }
    co_return;
}