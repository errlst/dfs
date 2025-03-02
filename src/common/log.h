// #pragma once
// #include <condition_variable>
// #include <cstring>
// #include <fstream>
// #include <mutex>
// #include <print>
// #include <source_location>

#pragma once
#include <print>

// #define NO_LOG

#ifdef NO_LOG
#define LOG_NO_DEBUG
#define LOG_NO_INFO
#define LOG_NO_ERROR
#endif

#ifdef LOG_NO_DEBUG
#define LOG_DEBUG(msg)
#else
#define LOG_DEBUG(msg) std::println("\033[34m [{}:{}]\033[0m {}", __FILE__, __LINE__, msg)
#endif

#ifdef LOG_NO_INFO
#define LOG_INFO(msg)
#else
#define LOG_INFO(msg) std::println("\033[32m [{}:{}]\033[0m {}", __FILE__, __LINE__, msg)
#endif

#ifdef LOG_NO_ERROR
#define LOG_ERROR(msg)
#else
#define LOG_ERROR(msg) std::println("\033[31m [{}:{}]\033[0m] {}", __FILE__, __LINE__, msg)
#endif

// enum class log_level_e : uint8_t {
//   debug = 0,
//   info = 1,
//   warn = 2,
//   error = 3,
//   fatal = 4,
//   invalid
// };

// class log_t {
// public:
//   log_t(std::string_view path, log_level_e level, bool daemon);

//   ~log_t() = default;

//   auto log_debug(const std::string &message, std::source_location loc = std::source_location::current()) -> void;

//   auto log_info(const std::string &message, std::source_location loc = std::source_location::current()) -> void;

//   auto log_warn(const std::string &message, std::source_location loc = std::source_location::current()) -> void;

//   auto log_error(const std::string &message, std::source_location loc = std::source_location::current()) -> void;

//   auto log_fatal(const std::string &message, std::source_location loc = std::source_location::current()) -> void;

// private:
//   // auto run() -> void;

//   auto do_log(log_level_e level, const std::source_location &loc, const std::string &message) -> void;

//   auto log_level_str(log_level_e level) -> std::string;

//   auto update_time() -> void;

// private:
//   std::ofstream m_ofs;
//   std::mutex m_mut;
//   bool m_daemon;
//   log_level_e m_level;
//   std::string m_cached_time;
// };