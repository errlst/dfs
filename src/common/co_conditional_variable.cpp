#include "./co_conditional_variable.h"

auto co_conditional_variable_t::wait() -> asio::awaitable<void> {
    co_await asio::post(m_strand, asio::use_awaitable);
    auto timer = asio::steady_timer{m_strand};
    m_waiters.emplace([&timer] { timer.cancel(); });
    timer.expires_after(std::chrono::hours{24 * 365});
    co_await timer.async_wait(asio::as_tuple(asio::use_awaitable));
    co_return;
}

auto co_conditional_variable_t::notify_one() -> asio::awaitable<void> {
    co_await asio::post(m_strand, asio::use_awaitable);
    if (!m_waiters.empty()) {
        auto waiter = std::move(m_waiters.front());
        m_waiters.pop();
        waiter();
    }
    co_return;
}

auto co_conditional_variable_t::notify_all() -> asio::awaitable<void> {
    co_await asio::post(m_strand, asio::use_awaitable);
    while (!m_waiters.empty()) {
        auto waiter = std::move(m_waiters.front());
        m_waiters.pop();
        waiter();
    }
    co_return;
}