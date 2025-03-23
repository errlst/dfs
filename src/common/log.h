#pragma once

#define SPDLOG_USE_STD_FORMAT
#include <spdlog/spdlog.h>

#define LOG_DEBUG(fmt, ...) spdlog::default_logger()->log(spdlog::source_loc{__FILE__, __LINE__, __func__}, spdlog::level::debug, fmt, ##__VA_ARGS__)
#define LOG_INFO(fmt, ...) spdlog::default_logger()->log(spdlog::source_loc{__FILE__, __LINE__, __func__}, spdlog::level::info, fmt, ##__VA_ARGS__)
#define LOG_ERROR(fmt, ...) spdlog::default_logger()->log(spdlog::source_loc{__FILE__, __LINE__, __func__}, spdlog::level::err, fmt, ##__VA_ARGS__)
#define LOG_WARN(fmt, ...) spdlog::default_logger()->log(spdlog::source_loc{__FILE__, __LINE__, __func__}, spdlog::level::warn, fmt, ##__VA_ARGS__)
#define LOG_CRITICAL(fmt, ...) spdlog::default_logger()->log(spdlog::source_loc{__FILE__, __LINE__, __func__}, spdlog::level::critical, fmt, ##__VA_ARGS__)