#include "log.h"
#include <iostream>
#include <utility>

#define BLUE "\033[34m"
#define GREEN "\033[32m"
#define YELLOW "\033[33m"
#define RED "\033[31m"
#define MAGENTA "\033[35m"
#define RESET "\033[0m"

log_t::log_t(std::string_view path, log_level_e level, bool daemon) {
    m_level = level;
    m_ofs.open(path.data());
    if (!m_ofs.is_open()) {
        std::cerr << "failed to open log file: " << path << "\n";
        exit(-1);
    }
    m_daemon = daemon;
    update_time();
    std::thread{&log_t::run, this}.detach();
}

auto log_t::run() -> void {
    while (true) {
        auto lock = std::unique_lock{m_mut};
        m_cv.wait(lock, [this] { return !m_buf.empty(); });

        m_ofs << m_buf;
        if (!m_daemon) {
            std::cout << m_buf;
        }
        m_ofs.flush();
        m_buf.clear();

        update_time();
    }
}

auto log_t::log_debug(const std::string &message, std::source_location loc) -> void {
    do_log(log_level_e::debug, loc, message);
}

auto log_t::log_info(const std::string &message, std::source_location loc) -> void {
    do_log(log_level_e::info, loc, message);
}

auto log_t::log_warn(const std::string &message, std::source_location loc) -> void {
    do_log(log_level_e::warn, loc, message);
}

auto log_t::log_error(const std::string &message, std::source_location loc) -> void {
    do_log(log_level_e::error, loc, message);
}

auto log_t::log_fatal(const std::string &message, std::source_location loc) -> void {
    do_log(log_level_e::fatal, loc, message);
}

auto log_t::do_log(log_level_e level, const std::source_location &loc, const std::string &message) -> void {
    if (level < m_level) {
        return;
    }
    auto color = RED;
    switch (level) {
        case log_level_e::debug:
            color = BLUE;
            break;
        case log_level_e::info:
            color = GREEN;
            break;
        case log_level_e::warn:
            color = YELLOW;
            break;
        case log_level_e::error:
            color = RED;
            break;
        case log_level_e::fatal:
            color = MAGENTA;
            break;
        default:
            break;
    }
    auto fmted_str = std::format("{} [{}] [{}] [{}:{}] {} {}\n", color,
                                 m_cached_time, log_level_str(level),
                                 loc.file_name(), loc.line(), RESET, message);
    auto lock = std::unique_lock{m_mut};
    m_buf += fmted_str;
    m_cv.notify_one();
}

auto log_t::log_level_str(log_level_e level) -> std::string {
    switch (level) {
        case log_level_e::debug:
            return "DEBUG";
        case log_level_e::info:
            return "INFO";
        case log_level_e::warn:
            return "WARN";
        case log_level_e::error:
            return "ERROR";
        case log_level_e::fatal:
            return "FATAL";
        default:
            return "INVALID";
    }
    std::unreachable();
}

auto log_t::update_time() -> void {
    auto now = std::chrono::system_clock::now();
    auto now_c = std::chrono::system_clock::to_time_t(now);
    m_cached_time = std::ctime(&now_c);
    m_cached_time.pop_back(); // 删除换行符
}