#pragma once

#include <cstdint>
#include <generator>
#include <nlohmann/json.hpp>
#include <string_view>

/**
 * @brief 获取路径的可用空间
 *
 * @param path
 * @return <free_space, total_space>
 */
auto disk_space(std::string_view path) -> std::tuple<uint64_t, uint64_t>;

/**
 * @brief 生成指定长度随机字符串
 *
 */
auto random_string(uint32_t len) -> std::string;

/**
 * @brief 更大字节的网络和本地字节序转换
 *
 */
auto htonll(uint64_t host_value) -> uint64_t;
auto ntohll(uint64_t net_value) -> uint64_t;

/**
 * @brief 通用读取配置文件
 *
 */
auto read_config(std::string_view path) -> nlohmann::json;

/**
 * @brief 初始化 base_path
 *
 */
auto init_base_path(const nlohmann::json &json) -> void;

/**
 * @brief 递归遍历目录下的所有普通文件
 *
 * @return 文件的绝对路径
 */
auto iterate_normal_file(std::string_view path) -> std::generator<std::string>;

/**
 * @brief 一些整数字面量常量
 *
 */
inline auto operator""_KB(unsigned long long val) -> uint64_t { return val * 1024; }
inline auto operator""_MB(unsigned long long val) -> uint64_t { return val * 1024 * 1024; }
inline auto operator""_minute(unsigned long long val) -> uint64_t { return val * 60; }
inline auto operator""_hour(unsigned long long val) -> uint64_t { return val * 60 * 60; }
inline auto operator""_day(unsigned long long val) -> uint64_t { return val * 60 * 60 * 24; }