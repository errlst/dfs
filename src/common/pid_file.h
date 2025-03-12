#pragma once
#include <string_view>

namespace common {
/**
 * @brief 写入 pid 到 <base_path>/data/pid.txt
 *
 * @param program 程序名
 * @param force 强制写入
 */
auto write_pid_file(std::string_view program, std::string_view base_path, bool force) -> void;

/**
 * @brief 读取 pid
 *
 */
auto read_pid_file(std::string_view base_path) -> int;

/**
 * @brief 删除 pid 文件
 *
 */
auto remove_pid_file(std::string_view base_path) -> void;

} // namespace common
