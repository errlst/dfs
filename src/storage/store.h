#pragma once
#include <atomic>
#include <fstream>
#include <map>
#include <memory>
#include <random>
#include <string>
#include <variant>
#include <vector>

/*
  相对路径为 xx/xx/<filename>
*/
class store_ctx {
public:
  store_ctx(const std::string &path);

  ~store_ctx() = default;

  auto create_file(uint64_t file_id, uint64_t file_size) -> bool;
  auto create_file(uint64_t file_id, uint64_t file_size, std::string_view rel_path) -> bool;

  auto write_file(uint64_t file_id, std::span<char> data) -> bool;

  /* 返回最后的相对路径 */
  auto close_file(uint64_t file_id, std::string_view filename) -> std::optional<std::string>;
  auto close_file(uint64_t file_id) -> bool;

  auto open_file(uint64_t file_id, std::string_view rel_path) -> std::optional<uint64_t>;

  auto read_file(uint64_t file_id, uint64_t offset, uint64_t size) -> std::optional<std::vector<char>>;
  auto read_file(uint64_t file_id, uint64_t size) -> std::optional<std::vector<char>>;
  auto read_file(uint64_t file_id, char *dst, uint64_t size) -> uint64_t;

  auto free_space() -> uint64_t;

  auto base_path() -> std::string;

private:
  /* 检查磁盘空间是否够，留 5% */
  auto check_disk_enough(uint64_t size) -> bool;

  auto relative_path(uint16_t idx) -> std::string;

  auto absolute_path(std::string_view rel_path) -> std::string;

  auto next_idx() -> uint16_t;

  auto get_ifstream(uint64_t file_id) -> std::optional<std::tuple<std::string, std::shared_ptr<std::ifstream>>>;
  auto get_ofstream(uint64_t file_id) -> std::optional<std::tuple<std::string, std::shared_ptr<std::ofstream>>>;

private:
  std::string m_base_path; // basepath/xx/xx/filename == basepath/relpath
  std::shared_ptr<std::atomic_uint16_t> m_idx = std::make_shared<std::atomic_uint16_t>(0);
  std::map<uint64_t, std::tuple<std::string, std::variant<std::shared_ptr<std::ofstream>, std::shared_ptr<std::ifstream>>>> m_files = {}; // 存放相对路径
  std::default_random_engine m_rnd = std::default_random_engine(std::random_device()());
};

class store_ctx_group {
public:
  store_ctx_group(const std::string &name, const std::vector<std::string> &path);

  ~store_ctx_group() = default;

  /* 创建文件 返回 file_id */
  auto create_file(uint64_t file_size) -> std::optional<uint64_t>;
  auto create_file(uint64_t file_size, std::string_view rel_path) -> std::optional<uint64_t>;

  /* append 写入文件 */
  auto write_file(uint64_t file_id, std::span<char> data) -> bool;

  // 返回：relpath
  auto close_file(uint64_t file_id, std::string_view filename) -> std::optional<std::string>;
  auto close_file(uint64_t file_id) -> bool;

  // 打开文件之后可以读取文件内容
  // 返回: (file_id, file_size)
  auto open_file(std::string_view rel_path) -> std::optional<std::pair<uint64_t, uint64_t>>;

  auto read_file(uint64_t file_id, uint64_t offset, uint64_t size) -> std::optional<std::vector<char>>;
  auto read_file(uint64_t file_id, uint64_t size) -> std::optional<std::vector<char>>;
  auto read_file(uint64_t file_id, char *dst, uint64_t size) -> uint64_t;

  /* 最大空闲空间 */
  auto max_free_space() -> uint64_t;

  auto next_path() -> std::string;

  auto valid() -> bool;

  /* <path, avaliable, total> */
  auto monitor_disk() -> std::vector<std::tuple<std::string, uint64_t, uint64_t>>;

private:
  auto next_idx() -> uint16_t;

  auto next_file_id() -> uint64_t;

  auto store_idx(uint16_t idx) -> uint16_t;

  auto map_to_store_idx(uint64_t file_id) -> std::optional<uint16_t>;

private:
  std::string m_name;
  std::vector<store_ctx> m_stores = {};
  std::shared_ptr<std::atomic_uint16_t> m_idx = std::make_shared<std::atomic_uint16_t>(0); // m_idx % m_stores.size() == store_idx
  std::shared_ptr<std::atomic_uint64_t> m_file_id = std::make_shared<std::atomic_uint64_t>(0);
  std::map<uint64_t, uint16_t> m_store_idx_map = {}; // file_id -> store_idx
};