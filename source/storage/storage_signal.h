#pragma once
#include <string_view>

/**
 * @brief 初始化信号处理程序
 *
 */
auto init_storage_signal() -> bool;

/**
 * @brief 处理信号
 *
 */
auto signal_process(std::string_view sig, int pid) -> void;