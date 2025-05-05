#pragma once

#define SPDLOG_USE_STD_FORMAT
#include <spdlog/spdlog.h>
#include <string_view>

namespace common
{

  enum class log_level
  {
    _, // 对齐 spdlog::level::level_enum
    debug,
    info,
    error,
    warn,
    critical,
    invalid,
  };

  /**
   * @brief 初始化日志，使用 daily_sink
   *
   * @param base_path 在 <base_path>/log 目录下生成日志文件
   * @param daemon 如果不是守护进程，还会增加 stdout_color_sink_mt
   * @return auto
   */
  auto init_log(std::string_view base_path, bool daemon, log_level level = log_level::debug) -> void;

} // namespace common

#define LOG_DEBUG(fmt, ...) spdlog::default_logger()->log(spdlog::source_loc{__FILE__, __LINE__, __func__}, spdlog::level::debug, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...) spdlog::default_logger()->log(spdlog::source_loc{__FILE__, __LINE__, __func__}, spdlog::level::info, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) spdlog::default_logger()->log(spdlog::source_loc{__FILE__, __LINE__, __func__}, spdlog::level::err, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...) spdlog::default_logger()->log(spdlog::source_loc{__FILE__, __LINE__, __func__}, spdlog::level::warn, fmt, ##__VA_ARGS__)
#define LOG_CRITICAL(fmt, ...) spdlog::default_logger()->log(spdlog::source_loc{__FILE__, __LINE__, __func__}, spdlog::level::critical, fmt, ##__VA_ARGS__)