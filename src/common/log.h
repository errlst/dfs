#pragma
#include <condition_variable>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>
#include <source_location>
#include <thread>
#include <utility>

enum class log_level_e {
    debug = 0,
    info = 1,
    warn = 2,
    error = 3,
    fatal = 4,
};

class log_t {
  public:
    log_t() = default;
    ~log_t() = default;

    auto init(std::string_view path, log_level_e level, bool daemon) -> void {
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

    auto run() -> void {
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

    auto log_debug(const std::string &message, std::source_location loc = std::source_location::current()) -> void {
        do_log(log_level_e::debug, loc, message);
    }

    auto log_info(const std::string &message, std::source_location loc = std::source_location::current()) -> void {
        do_log(log_level_e::info, loc, message);
    }

    auto log_warn(const std::string &message, std::source_location loc = std::source_location::current()) -> void {
        do_log(log_level_e::warn, loc, message);
    }

    auto log_error(const std::string &message, std::source_location loc = std::source_location::current()) -> void {
        do_log(log_level_e::error, loc, message);
    }

    auto log_fatal(const std::string &message, std::source_location loc = std::source_location::current()) -> void {
        do_log(log_level_e::fatal, loc, message);
    }

  private:
    // [log_level] [time] [file:line] [message]
    auto do_log(log_level_e level, const std::source_location &loc, const std::string &message) -> void {
        if (level < m_level) {
            return;
        }

        auto fmted_str = std::string{};
        if (errno) {
            fmted_str = std::format("[{}] [{}] [{}:{}] {}\terror: {}\n",
                                    m_cached_time, log_level_str(level),
                                    loc.file_name(), loc.line(), message, strerror(errno));
            errno = 0;
        } else {
            fmted_str = std::format("[{}] [{}] [{}:{}] {}\n",
                                    m_cached_time, log_level_str(level),
                                    loc.file_name(), loc.line(), message);
        }

        auto lock = std::unique_lock{m_mut};
        m_buf += fmted_str;
        m_cv.notify_one();
    }

    auto log_level_str(log_level_e level) -> std::string {
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
        }
        std::unreachable();
    }

    auto update_time() -> void {
        auto now = std::chrono::system_clock::now();
        auto now_c = std::chrono::system_clock::to_time_t(now);
        m_cached_time = std::ctime(&now_c);
        m_cached_time.pop_back(); // 删除换行符
    }

  private:
    std::ofstream m_ofs;
    std::string m_buf;
    std::mutex m_mut;
    std::condition_variable m_cv;
    bool m_daemon;
    log_level_e m_level;
    std::string m_cached_time;
};