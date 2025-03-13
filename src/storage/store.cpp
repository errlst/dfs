#include "./store.h"
#include "../common/log.h"
#include "../common/util.h"
#include <filesystem>
#include <fstream>
#include <mutex>

store_ctx::store_ctx(std::string_view root_path)
    : m_root_path{root_path} {
  LOG_INFO(std::format("start init store '{}'", root_path));
  std::filesystem::create_directories(root_path);

  for (auto i = 0; i < 256; i++) {
    for (auto j = 0; j < 256; ++j) {
      std::filesystem::create_directories(std::format("{}/{:02X}/{:02X}", root_path, i, j));
    }
  }
  std::tie(m_disk_free, m_disk_total) = common::disk_space(root_path);
  LOG_INFO(std::format("init store '{}' suc", root_path));
}

auto store_ctx::create_file(uint64_t file_id, uint64_t file_size) -> bool {
  if (!disk_space_is_enough(file_size)) {
    LOG_ERROR(std::format("store '{}' has no enough space for {}GB, left {}GB", m_root_path, 1.0 * file_size / 1024 / 1024 / 1024, 1.0 * m_disk_free / 1024 / 1024 / 1024));
    return false;
  }

  auto rel_path = valid_rel_path(next_flat_path());
  auto ofs = create_ofstream(rel_path, file_size);
  if (!ofs) {
    return false;
  }

  {
    auto lock = std::unique_lock{m_ofstream_mut};
    m_ofstreams[file_id] = std::make_pair(ofs, std::move(rel_path));
  }

  reduce_disk_free(file_size);
  return true;
}

auto store_ctx::create_file(uint64_t file_id, uint64_t file_size, std::string_view rel_path) -> bool {
  if (!disk_space_is_enough(file_size)) {
    LOG_ERROR(std::format("store '{}' has no enough space for {}GB, left {}GB", m_root_path, 1.0 * file_size / 1024 / 1024 / 1024, 1.0 * m_disk_free / 1024 / 1024 / 1024));
    return false;
  }

  auto abs_path = std::format("{}/{}", m_root_path, rel_path);
  if (std::filesystem::exists(abs_path)) {
    return false;
  }

  auto ofs = create_ofstream(rel_path, file_size);
  if (!ofs) {
    return false;
  }

  {
    auto lock = std::unique_lock{m_ofstream_mut};
    m_ofstreams[file_id] = std::make_pair(ofs, rel_path);
  }

  reduce_disk_free(file_size);
  return true;
}

auto store_ctx::append_data(uint64_t file_id, std::span<char> data) -> bool {
  auto [ofs, _] = peek_ofstream(file_id);
  if (!ofs) {
    LOG_ERROR(std::format("invalid file_id {}", file_id));
    return false;
  }

  ofs->write(data.data(), data.size());
  if (!ofs->good()) {
    LOG_ERROR(std::format("write file failed for file_id {}", file_id));
    return false;
  }
  return true;
}

auto store_ctx::close_write_file(uint64_t file_id, std::string_view user_file_name) -> std::optional<std::pair<std::string, std::string>> {
  auto [ofs, rel_path] = pop_ofstream(file_id);
  if (!ofs) {
    LOG_ERROR(std::format("invalid file_id {}", file_id));
    return std::nullopt;
  }

  /* 重命名 TODO)) 增加 new_abs_path 有效检测*/
  auto flat_path = flat_of_rel_path(rel_path);
  auto old_abs_path = std::format("{}/{}", m_root_path, rel_path);
  auto new_rel_path = std::format("{}/{}_{}", flat_path, user_file_name, common::random_string(8));
  auto new_abs_path = std::format("{}/{}", m_root_path, new_rel_path);
  try {
    std::filesystem::rename(old_abs_path, new_abs_path);
  } catch (const std::runtime_error &err) {
    LOG_ERROR(std::format("rename from '{}' to '{}' failed, what: ", old_abs_path, new_abs_path, err.what()));
    return std::nullopt;
  }
  return std::pair{m_root_path, new_rel_path};
}

auto store_ctx::close_write_file(uint64_t file_id) -> std::optional<std::pair<std::string, std::string>> {
  auto [ofs, rel_path] = pop_ofstream(file_id);
  if (!ofs) {
    return std::nullopt;
  }

  ofs->close();
  return std::pair{m_root_path, rel_path};
}

auto store_ctx::open_read_file(uint64_t file_id, std::string_view rel_path) -> std::optional<uint64_t> {
  auto abs_path = std::format("{}/{}", m_root_path, rel_path);
  auto ifs = std::make_shared<std::ifstream>(abs_path, std::ios::binary | std::ios::ate);
  if (!ifs->is_open()) {
    return std::nullopt;
  }

  auto file_size = ifs->tellg();
  ifs->seekg(0);

  {
    auto lock = std::unique_lock{m_ifstreams_mtx};
    m_ifstreams[file_id] = std::make_pair(ifs, rel_path);
  }
  return file_size;
}

auto store_ctx::read_file(uint64_t file_id, uint64_t size) -> std::optional<std::vector<char>> {
  auto [ifs, _] = peek_ifstream(file_id);
  if (!ifs || !ifs->good()) {
    return std::nullopt;
  }

  if (ifs->eof()) {
    return std::vector<char>{};
  }

  auto data = std::vector<char>(size, 0);
  data.resize(ifs->readsome((char *)data.data(), size));
  if (!ifs->good()) {
    return std::nullopt;
  }
  return data;
}

auto store_ctx::read_file(uint64_t file_id, char *dst, uint64_t size) -> std::optional<uint64_t> {
  auto [ifs, _] = peek_ifstream(file_id);
  if (!ifs || !ifs->good()) {
    LOG_ERROR("read file failed");
    return std::nullopt;
  }

  if (ifs->eof()) {
    return 0;
  }

  return ifs->readsome(dst, size);
}

auto store_ctx::close_read_file(uint64_t file_id) -> bool {
  auto [ifs, _] = pop_ifstream(file_id);
  if (!ifs) {
    return false;
  }
  ifs->close();
  return true;
}

auto store_ctx::free_space() -> uint64_t {
  static auto times = 0;
  if (++times % 10 == 0) {
    std::tie(m_disk_free, m_disk_total) = common::disk_space(m_root_path);
  }
  return m_disk_free;
}

auto store_ctx::peek_ofstream(uint64_t file_id) -> std::pair<std::shared_ptr<std::ofstream>, std::string> {
  auto lock = std::unique_lock{m_ofstream_mut};
  auto it = m_ofstreams.find(file_id);
  if (it == m_ofstreams.end()) {
    LOG_ERROR("invalid file_id {}", file_id);
    return {nullptr, ""};
  }
  return it->second;
}

auto store_ctx::pop_ofstream(uint64_t file_id) -> std::pair<std::shared_ptr<std::ofstream>, std::string> {
  auto lock = std::unique_lock{m_ofstream_mut};
  auto it = m_ofstreams.find(file_id);
  if (it == m_ofstreams.end()) {
    LOG_ERROR("invalid file_id {}", file_id);
    return {nullptr, ""};
  }
  auto res = std::move(it->second);
  m_ofstreams.erase(it);
  return res;
}

auto store_ctx::peek_ifstream(uint64_t file_id) -> std::pair<std::shared_ptr<std::ifstream>, std::string> {
  auto lock = std::unique_lock{m_ifstreams_mtx};
  auto it = m_ifstreams.find(file_id);
  if (it == m_ifstreams.end()) {
    return {nullptr, ""};
  }
  return it->second;
}

auto store_ctx::pop_ifstream(uint64_t file_id) -> std::pair<std::shared_ptr<std::ifstream>, std::string> {
  auto lock = std::unique_lock{m_ifstreams_mtx};
  auto it = m_ifstreams.find(file_id);
  if (it == m_ifstreams.end()) {
    return {nullptr, ""};
  }
  auto res = std::move(it->second);
  m_ifstreams.erase(it);
  return res;
}

auto store_ctx::create_ofstream(std::string_view rel_path, uint64_t file_size) -> std::shared_ptr<std::ofstream> {
  auto abs_path = std::format("{}/{}", m_root_path, rel_path);
  auto ofs = std::make_shared<std::ofstream>(abs_path, std::ios::binary);
  if (!ofs->is_open()) {
    return nullptr;
  }

  ofs->seekp(file_size - 1);
  ofs->put(0);
  ofs->seekp(0);
  return ofs;
}

auto store_ctx::next_flat_path() -> std::string {
  auto idx = m_flat_idx++;
  return std::format("{:02X}/{:02X}", idx >> 8, idx & 0xFF);
}

auto store_ctx::flat_of_rel_path(std::string_view rel_path) -> std::string {
  return std::string{rel_path.substr(0, rel_path.find_last_of('/'))};
}

auto store_ctx::valid_rel_path(std::string_view flat_path) -> std::string {
  auto rel_path = std::format("{}/{}", flat_path, common::random_string(8));
  while (std::filesystem::exists(std::format("{}/{}", m_root_path, rel_path))) {
    rel_path = std::format("{}/{}", flat_path, common::random_string(8));
  }
  return rel_path;
}

auto store_ctx::disk_space_is_enough(uint64_t size) -> bool {
  if (m_disk_free < size || m_disk_free < m_disk_total * 0.05) {
    return false;
  }
  return true;
}

store_ctx_group::store_ctx_group(const std::string &name, const std::vector<std::string> &paths)
    : m_name{name} {
  for (const auto &path : paths) {
    m_stores.emplace_back(std::make_shared<store_ctx>(path));
  }
}

auto store_ctx_group::create_file(uint64_t file_size) -> std::optional<std::uint64_t> {
  for (auto [s, file_id] : iterate_store(++m_store_idx)) {
    if (s->create_file(file_id, file_size)) {
      return file_id;
    }
  }
  return std::nullopt;
}

auto store_ctx_group::create_file(uint64_t file_size, std::string_view rel_path) -> std::optional<uint64_t> {
  for (auto [s, file_id] : iterate_store(++m_store_idx)) {
    if (s->create_file(file_id, file_size, rel_path)) {
      return file_id;
    }
  }
  return std::nullopt;
}

auto store_ctx_group::write_file(uint64_t file_id, std::span<char> data) -> bool {
  return m_stores[file_id % m_stores.size()]->append_data(file_id, data);
}

auto store_ctx_group::open_read_file(std::string_view rel_path) -> std::optional<std::tuple<uint64_t, uint64_t, std::string>> {
  for (auto [s, file_id] : iterate_store(++m_store_idx)) {
    if (auto file_size = s->open_read_file(file_id, rel_path); file_size.has_value()) {
      return std::tuple{file_id, file_size.value(), std::format("{}/{}", s->root_path(), rel_path)};
    }
  }

  return std::nullopt;
}

auto store_ctx_group::read_file(uint64_t file_id, uint64_t size) -> std::optional<std::vector<char>> {
  return m_stores[file_id % m_stores.size()]->read_file(file_id, size);
}

auto store_ctx_group::read_file(uint64_t file_id, char *dst, uint64_t size) -> std::optional<uint64_t> {
  return m_stores[file_id % m_stores.size()]->read_file(file_id, dst, size);
}

auto store_ctx_group::max_free_space() -> uint64_t {
  auto res = uint64_t{0};
  for (auto store : m_stores) {
    res = std::max(res, store->free_space());
  }
  return res;
}

auto store_ctx_group::iterate_store(uint64_t start_idx) -> std::generator<std::pair<std::shared_ptr<store_ctx>, uint64_t>> {
  for (auto i = 0uz, file_id = start_idx, count = m_stores.size(); i < count; ++i, ++file_id) {
    co_yield {m_stores[file_id % count], file_id};
  }
}
