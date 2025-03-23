module;

#include <string_view>

export module common.log;

export namespace common {
  
  enum class log_level {
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
