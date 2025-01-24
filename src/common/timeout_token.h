#pragma once

#include <asio.hpp>
#include <asio/experimental/parallel_group.hpp>
#include <print>

using tcp = asio::ip::tcp;

template <typename InnerToken>
struct timeout_token_t {
    InnerToken token;
    std::chrono::milliseconds timeout;
};

template <typename InnerToken>
auto timeout_token(InnerToken token, std::chrono::milliseconds t) -> timeout_token_t<InnerToken> {
    return {token, t};
}

template <typename... Signatures>
struct initiate_timeout {

    template <typename InnerToken, typename Initiation, typename... Args>
    auto operator()(InnerToken token, std::chrono::milliseconds timeout, Initiation &&initiation, Args &&...args) -> void {
        auto ex = asio::get_associated_executor(token, asio::get_associated_executor(initiation));
        auto alloc = asio::get_associated_allocator(token);
        auto timer = std::allocate_shared<asio::steady_timer>(alloc, ex, timeout);
        asio::experimental::make_parallel_group(
            asio::bind_executor(ex, [&](auto &&token) {
                return timer->async_wait(std::forward<decltype(token)>(token));
            }),
            asio::bind_executor(ex, [&](auto &&token) {
                return asio::async_initiate<decltype(token), Signatures...>(std::forward<Initiation>(initiation), token,
                                                                            std::forward<Args>(args)...);
            }))
            .async_wait(asio::experimental::wait_for_one(), [token = std::move(token), timer](std::array<std::size_t, 2>, std::error_code ec, auto... underlying_op_results) mutable {
                timer.reset();
                std::move(token)(std::move(underlying_op_results)...);
            });
    }
};

template <typename InnerToken, typename... Signatures>
struct asio::async_result<timeout_token_t<InnerToken>, Signatures...> {

    template <typename Initiation, typename... Args>
    static auto initiate(Initiation &&init, timeout_token_t<InnerToken> tt, Args &&...args) {
        return asio::async_initiate<InnerToken, Signatures...>(
            initiate_timeout<Signatures...>{},
            tt.token,
            tt.timeout,
            std::forward<Initiation>(init),
            std::forward<Args>(args)...);
    }
};