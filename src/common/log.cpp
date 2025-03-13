#include "log.h"

auto common::init_log(std::string_view base_path, bool daemon, log_level level) -> void {
  auto logger = std::make_shared<spdlog::logger>("");
  logger->sinks().push_back(std::make_shared<spdlog::sinks::daily_file_sink_mt>(std::format("{}/log/log.log", base_path), 0, 0, 1024));
  if (!daemon) {
    logger->sinks().push_back(std::make_shared<spdlog::sinks::stdout_color_sink_mt>());
  }
  logger->set_pattern("[%Y-%m-%d %H:%M:%S.%e] %^[%l] [%s:%#]%$ %v");
  spdlog::set_default_logger(logger);
  spdlog::set_level(static_cast<spdlog::level::level_enum>(level));
}