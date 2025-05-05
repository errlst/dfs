#include <common/log.h>
#include <common/util.h>
#include <fstream>
#include <random>
#include <sys/statvfs.h>

namespace common
{
  auto disk_space(std::string_view path) -> std::tuple<uint64_t, uint64_t>
  {
    struct statvfs stat;
    if (statvfs(path.data(), &stat) != 0)
    {
      return std::make_tuple(0ull, 0ull);
    }
    return std::make_tuple(stat.f_bavail * stat.f_bsize, stat.f_blocks * stat.f_bsize);
  }

  auto random_string(uint32_t len) -> std::string
  {
    static constexpr char chars[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    static auto rng = std::default_random_engine(std::random_device()());
    auto res = std::string(len, 0);
    for (auto i = 0u; i < len; ++i)
    {
      res[i] = chars[rng() % (sizeof(chars) - 1)];
    }
    return res;
  }

  auto htonll(uint64_t data) -> uint64_t
  {
    if constexpr (std::endian::native == std::endian::little)
    {
      return ((data & 0xFF00000000000000ULL) >> 56) |
             ((data & 0x00FF000000000000ULL) >> 40) |
             ((data & 0x0000FF0000000000ULL) >> 24) |
             ((data & 0x000000FF00000000ULL) >> 8) |
             ((data & 0x00000000FF000000ULL) << 8) |
             ((data & 0x0000000000FF0000ULL) << 24) |
             ((data & 0x000000000000FF00ULL) << 40) |
             ((data & 0x00000000000000FFULL) << 56);
    }
    else
    {
      return data;
    }
  }

  auto ntohll(uint64_t data) -> uint64_t
  {
    return htonll(data);
  }

  auto read_config(std::string_view path) -> nlohmann::json
  {
    auto ifs = std::ifstream{path.data()};
    if (!ifs)
    {
      LOG_CRITICAL("failed open config file {}", path);
      exit(-1);
    }

    auto json = nlohmann::json::parse(ifs, nullptr, false, true); // 允许 json 注释
    if (json.empty())
    {
      LOG_CRITICAL("failed parse configure '{}'", path);
      exit(-1);
    }
    LOG_INFO("read config file {} suc", path);
    return json;
  }

  auto init_base_path(std::string_view base_path) -> void
  {
    try
    {
      std::filesystem::create_directories(std::format("{}/data", base_path));
    }
    catch (...)
    {
      LOG_CRITICAL("init base path {} failed", base_path);
      exit(-1);
    }
    LOG_INFO("init base path {} suc", base_path);
  }

  auto iterate_normal_file(std::string_view path) -> std::generator<std::string>
  {
    for (const auto &dir_entry : std::filesystem::recursive_directory_iterator(path))
    {
      if (dir_entry.is_regular_file())
      {
        co_yield dir_entry.path().string();
      }
    }
  }

  auto bytes_to_hex_str(std::span<char> data) -> std::string
  {
    auto ret = std::string{};
    for (auto hex : data)
    {
      ret += std::format("{:02X} ", hex);
    }
    return ret;
  }

} // namespace common