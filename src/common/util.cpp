#include "util.h"
#include <filesystem>
#include <fstream>
#include <print>
#include <random>
#include <sys/statvfs.h>

auto check_directory(std::string_view path) -> void {
  if (std::filesystem::exists(path)) {
    if (!std::filesystem::is_directory(path)) {
      std::println("{} is not a directory", path);
      exit(-1);
    }
  } else {
    if (!std::filesystem::create_directories(path)) {
      std::println("failed to create directory: {}", path);
      exit(-1);
    }
  }
}

auto fs_free_size(std::string_view path) -> std::tuple<uint64_t, uint64_t> {
  struct statvfs stat;
  if (statvfs(path.data(), &stat) != 0) {
    return std::make_tuple(0ull, 0ull);
  }
  return std::make_tuple(stat.f_bavail * stat.f_bsize, stat.f_blocks * stat.f_bsize);
}

auto random_string(uint32_t len) -> std::string {
  constexpr auto chars = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
  static auto rng = std::default_random_engine(std::random_device()());
  auto res = std::string(len, 0);
  for (auto i = 0; i < len; ++i) {
    res[i] = chars[rng() % 62];
  }
  return res;
}

auto htonll(uint64_t host_value) -> uint64_t {
  if constexpr (std::endian::native == std::endian::little) {
    return ((host_value & 0xFF00000000000000ULL) >> 56) |
           ((host_value & 0x00FF000000000000ULL) >> 40) |
           ((host_value & 0x0000FF0000000000ULL) >> 24) |
           ((host_value & 0x000000FF00000000ULL) >> 8) |
           ((host_value & 0x00000000FF000000ULL) << 8) |
           ((host_value & 0x0000000000FF0000ULL) << 24) |
           ((host_value & 0x000000000000FF00ULL) << 40) |
           ((host_value & 0x00000000000000FFULL) << 56);
  } else {
    return host_value;
  }
}

auto ntohll(uint64_t net_value) -> uint64_t {
  return htonll(net_value); // 直接复用 htonll
}

auto read_config(std::string_view path) -> nlohmann::json {
  auto ifs = std::ifstream{path.data()};
  if (!ifs) {
    std::println("failed open configure '{}'", path);
    exit(-1);
  }

  auto json = nlohmann::json::parse(ifs, nullptr, false, true); // 允许 json 注释
  if (json.empty()) {
    std::println("failed parse configure '{}'", path);
    exit(-1);
  }
  std::println("read configure suc '{}'", path);
  return json;
}

auto init_base_path(const nlohmann::json &json) -> void {
  auto base_path = json["common"]["base_path"].get<std::string>();
  try {
    std::filesystem::create_directories(std::format("{}/data", base_path));
  } catch (...) {
    std::println("init base path failed '{}'", base_path);
    exit(-1);
  }
  std::println("init base path suc '{}'", base_path);
}