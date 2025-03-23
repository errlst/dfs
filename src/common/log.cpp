module;

#include "log.h"
#include <spdlog/sinks/daily_file_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/spdlog.h>

module common.log;

namespace common {

  auto init_log(std::string_view base_path, bool daemon, log_level level) -> void {
    if (std::to_underlying(level) >= std::to_underlying(log_level::invalid)) {
      LOG_CRITICAL("invalid log level");
      exit(-1);
    }

    auto logger = std::make_shared<spdlog::logger>("");
    logger->sinks().push_back(std::make_shared<spdlog::sinks::daily_file_sink_mt>(std::format("{}/log/log.log", base_path), 0, 0, 1024));
    if (!daemon) {
      logger->sinks().push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
    }
    logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] %^[%l] [%s:%#]%$ %v");
    spdlog::set_default_logger(logger);
    spdlog::set_level(static_cast<spdlog::level::level_enum>(level));
    spdlog::flush_on(spdlog::level::err);
  }
} // namespace common