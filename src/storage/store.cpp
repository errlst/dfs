#include "store.h"
#include "./global.h"
#include "global.h"
#include <cstring>
#include <sys/stat.h>
#include <sys/statvfs.h>

store_ctx_t::store_ctx_t(const std::string &path)
    : m_base_path{path} {
  if (mkdir(path.data(), 0755) != 0 && errno != EEXIST) {
    g_log->log_fatal(std::format("failed to create directory: {}", path));
    exit(-1);
  }

  for (auto i = 0; i < 256; i++) {
    auto sub_path_0 = std::format("{}/{:02X}", path, i);
    if (mkdir(sub_path_0.data(), 0755) != 0 && errno != EEXIST) {
      g_log->log_fatal(std::format("failed to create directory: {}", sub_path_0));
      exit(-1);
    }
    for (auto j = 0; j < 256; j++) {
      auto sub_path_1 = std::format("{}/{:02X}", sub_path_0, j);
      if (mkdir(sub_path_1.data(), 0755) != 0 && errno != EEXIST) {
        g_log->log_fatal(std::format("failed to create directory: {}", sub_path_0));
        exit(-1);
      }
    }
  }
  g_log->log_info(std::format("store {} init suc", path));
}

auto store_ctx_t::create_file(uint64_t file_id, uint64_t file_size) -> bool {
  if (!check_disk_enough(file_size)) {
    return false;
  }

  auto rel_path = std::format("{}/tmp_{}_{}", relative_path(next_idx()), m_rnd(), file_size);
  auto abs_path = absolute_path(rel_path);
  auto ofs = std::make_shared<std::ofstream>(abs_path, std::ios::binary | std::ios::trunc);
  if (!ofs->is_open()) {
    g_log->log_error(std::format("failed to create file: {}", abs_path));
    return false;
  }
  ofs->seekp(file_size - 1);
  ofs->put(0);
  ofs->seekp(0);
  m_files[file_id] = std::make_tuple(rel_path, ofs);
  g_log->log_debug(std::format("create file: {}", abs_path));
  return true;
}

auto store_ctx_t::create_file(uint64_t file_id, uint64_t file_size, std::string_view rel_path) -> bool {
  if (!check_disk_enough(file_size)) {
    return false;
  }

  auto abs_path = absolute_path(std::string{rel_path});
  auto ofs = std::make_shared<std::ofstream>(abs_path, std::ios::binary | std::ios::trunc);
  if (!ofs->is_open()) {
    g_log->log_error(std::format("failed to create file: {}", abs_path));
    return false;
  }
  ofs->seekp(file_size - 1);
  ofs->put(0);
  ofs->seekp(0);
  m_files[file_id] = std::make_tuple(rel_path, ofs);
  g_log->log_debug(std::format("create file: {}", abs_path));
  return true;
}

auto store_ctx_t::write_file(uint64_t file_id, std::span<uint8_t> data) -> bool {
  if (auto res = get_ofstream(file_id); res.has_value()) {
    const auto &[rel_path, ofs] = res.value();
    ofs->write((const char *)data.data(), data.size());
    if (!ofs->good()) {
      g_log->log_error(std::format("write file: {} failed", absolute_path(rel_path)));
      return false;
    }
    // g_log->log_debug(std::format("write file: {}, size: {} suc", absolute_path(rel_path), data.size()));
    return true;
  }
  return false;
}

auto store_ctx_t::close_file(uint64_t file_id, const std::string &filename) -> std::optional<std::string> {
  if (auto res = get_ofstream(file_id); res.has_value()) {
    m_files.erase(file_id);
    const auto &[old_rel_path, ofs] = res.value();
    ofs->close();
    auto old_base_path = std::format("{}/{}", m_base_path, old_rel_path);

    /* 拼接新的 relpath 和 abspath */
    auto new_rel_prefix = std::format("{}/{}", old_rel_path.substr(0, old_rel_path.find_last_of('/')), filename);
    auto new_rel_path = std::string{};
    auto new_abs_path = std::string{};
    do {
      new_rel_path = std::format("{}_{}", new_rel_prefix, random_string(8));
      new_abs_path = std::format("{}/{}", m_base_path, new_rel_path);
    } while (std::filesystem::exists(new_abs_path));

    /* 重命名 */
    g_log->log_debug(std::format("close file: {} -> {}", old_base_path, new_abs_path));
    std::filesystem::rename(old_base_path, new_abs_path);
    return new_rel_path;
  }
  return std::nullopt;
}

auto store_ctx_t::close_file(uint64_t file_id) -> bool {
  if (auto res = get_ofstream(file_id); res.has_value()) {
    m_files.erase(file_id);
    const auto &[rel_path, ofs] = res.value();
    ofs->close();
    g_log->log_debug(std::format("close file : {}", rel_path));
    return true;
  }
  return false;
}

auto store_ctx_t::open_file(uint64_t file_id, const std::string &rel_path) -> std::optional<uint64_t> {
  auto abs_path = absolute_path(rel_path);
  auto ifs = std::make_shared<std::ifstream>(abs_path, std::ios::binary | std::ios::ate);
  if (!ifs->is_open()) {
    return std::nullopt;
  }
  auto file_size = ifs->tellg();
  ifs->seekg(0);
  m_files[file_id] = std::make_tuple(rel_path, ifs);
  return file_size;
}

auto store_ctx_t::read_file(uint64_t file_id, uint64_t offset, uint64_t size) -> std::optional<std::vector<uint8_t>> {
  if (auto _ = get_ifstream(file_id); _.has_value()) {
    auto &[rel_path, ifs] = _.value();
    ifs->seekg(offset);
    if (!ifs->good()) {
      g_log->log_error(std::format("seekg file {} failed", absolute_path(rel_path)));
      return std::nullopt;
    }
    auto data = std::vector<uint8_t>(size, 0);
    data.resize(ifs->readsome((char *)data.data(), size));
    if (!ifs->good()) {
      g_log->log_error(std::format("read file {} failed", absolute_path(rel_path)));
      return std::nullopt;
    }
    g_log->log_debug(std::format("read file {} offset {} size {} suc", absolute_path(rel_path), offset, size));
    return data;
  }
  return std::nullopt;
}

auto store_ctx_t::read_file(uint64_t file_id, uint64_t size) -> std::optional<std::vector<uint8_t>> {
  if (auto _ = get_ifstream(file_id); _.has_value()) {
    auto &[rel_path, ifs] = _.value();
    if (ifs->eof()) {
      return std::vector<uint8_t>{};
    }
    if (!ifs->good()) {
      return std::nullopt;
    }
    auto data = std::vector<uint8_t>(size, 0);
    data.resize(ifs->readsome((char *)data.data(), size));
    if (!ifs->good()) {
      return std::nullopt;
    }
    return data;
  }
  return std::nullopt;
}

auto store_ctx_t::base_path() -> std::string {
  return m_base_path;
}

auto store_ctx_t::check_disk_enough(uint64_t size) -> bool {
  auto [free, total] = fs_free_size(m_base_path);
  if (free < size || free < total * 0.05) {
    g_log->log_error(std::format("store {} no enough space for {}GB, left {}GB", m_base_path, 1.0 * size / 1024 / 1024 / 1024, 1.0 * free / 1024 / 1024 / 1024));
    return false;
  }
  return true;
}

auto store_ctx_t::relative_path(uint16_t idx) -> std::string {
  return std::format("{:02X}/{:02X}", idx >> 8, idx & 0xFF);
}

auto store_ctx_t::absolute_path(const std::string &rel_path) -> std::string {
  return std::format("{}/{}", m_base_path, rel_path);
}

auto store_ctx_t::next_idx() -> uint16_t {
  return (*m_idx)++ % UINT16_MAX;
}

auto store_ctx_t::get_ifstream(uint64_t file_id) -> std::optional<std::tuple<std::string, std::shared_ptr<std::ifstream>>> {
  auto it = m_files.find(file_id);
  if (it == m_files.end()) {
    g_log->log_debug(std::format("invalid fileid", m_base_path, file_id));
    return std::nullopt;
  }
  const auto &[path, file] = it->second;
  if (file.index() != 1) {
    g_log->log_debug(std::format("invalid fileid", m_base_path, file_id));
    return std::nullopt;
  }
  return std::make_tuple(path, std::get<1>(file));
}

auto store_ctx_t::get_ofstream(uint64_t file_id) -> std::optional<std::tuple<std::string, std::shared_ptr<std::ofstream>>> {

  auto it = m_files.find(file_id);
  if (it == m_files.end()) {
    g_log->log_debug(std::format("invalid fileid {}", m_base_path, file_id));
    return std::nullopt;
  }
  const auto &[path, file] = it->second;
  if (file.index() != 0) {
    g_log->log_debug(std::format("invalid fileid", m_base_path, file_id));
    return std::nullopt;
  }
  return std::make_tuple(path, std::get<0>(file));
}

store_ctx_group_t::store_ctx_group_t(const std::string &name, const std::vector<std::string> &paths)
    : m_name{name} {
  for (const auto &path : paths) {
    m_stores.emplace_back(path);
  }
}

auto store_ctx_group_t::create_file(uint64_t file_size) -> std::optional<uint64_t> {
  auto file_id = next_file_id();
  for (auto i = 0, idx = (int)next_idx(); i < m_stores.size(); ++i, idx = (idx + 1) % m_stores.size()) {
    if (m_stores[idx].create_file(file_id, file_size)) {
      m_store_idx_map[file_id] = store_idx(idx);
      return {file_id};
    }
  }
  return std::nullopt;
}

auto store_ctx_group_t::create_file(uint64_t file_size, std::string_view rel_path) -> std::optional<uint64_t> {
  auto file_id = next_file_id();
  for (auto i = 0, idx = (int)next_idx(); i < m_stores.size(); ++i, idx = (idx + 1) % m_stores.size()) {
    if (m_stores[idx].create_file(file_id, file_size, rel_path)) {
      m_store_idx_map[file_id] = store_idx(idx);
      return {file_id};
    }
  }
  return std::nullopt;
}

auto store_ctx_group_t::write_file(uint64_t file_id, std::span<uint8_t> data) -> bool {
  if (auto idx = map_to_store_idx(file_id); idx.has_value()) {
    return m_stores[idx.value()].write_file(file_id, data);
  }
  return false;
}

auto store_ctx_group_t::close_file(uint64_t file_id, const std::string &filename) -> std::optional<std::string> {
  if (auto idx = map_to_store_idx(file_id); idx.has_value()) {
    m_store_idx_map.erase(file_id);
    return m_stores[idx.value()].close_file(file_id, filename);
  }
  return std::nullopt;
}

auto store_ctx_group_t::close_file(uint64_t file_id) -> bool {
  if (auto idx = map_to_store_idx(file_id); idx.has_value()) {
    m_store_idx_map.erase(file_id);
    return m_stores[idx.value()].close_file(file_id);
  }
  return false;
}

auto store_ctx_group_t::open_file(const std::string &relpath) -> std::optional<std::tuple<uint64_t, uint64_t>> {
  auto file_id = next_file_id();
  for (auto i = 0, idx = (int)next_idx(); i < m_stores.size(); ++i, idx = (idx + 1) % m_stores.size()) {
    if (auto res = m_stores[idx].open_file(file_id, relpath); res.has_value()) {
      m_store_idx_map[file_id] = idx;
      g_log->log_debug(std::format("store group ‘{}’ open file '{}' suc", m_name, relpath));
      return std::make_tuple(file_id, res.value());
    }
  }
  g_log->log_error(std::format("store group ‘{}’ no file '{}'", m_name, relpath));
  return std::nullopt;
}

auto store_ctx_group_t::read_file(uint64_t file_id, uint64_t offset, uint64_t size) -> std::optional<std::vector<uint8_t>> {
  if (auto idx = map_to_store_idx(file_id); idx.has_value()) {
    return m_stores[idx.value()].read_file(file_id, offset, size);
  }
  return std::nullopt;
}

auto store_ctx_group_t::read_file(uint64_t file_id, uint64_t size) -> std::optional<std::vector<uint8_t>> {
  if (auto idx = map_to_store_idx(file_id); idx.has_value()) {
    return m_stores[idx.value()].read_file(file_id, size);
  }
  return std::nullopt;
}

auto store_ctx_group_t::valid() -> bool {
  return m_stores.size() > 0;
}

auto store_ctx_group_t::monitor_disk() -> std::vector<std::tuple<std::string, uint64_t, uint64_t>> {
  auto res = std::vector<std::tuple<std::string, uint64_t, uint64_t>>{};
  struct statvfs svfs;
  for (auto &store : m_stores) {
    if (statvfs(store.base_path().c_str(), &svfs) != 0) {
      g_log->log_error(std::format("failed to get statvfs: {}", store.base_path()));
      res.emplace_back(store.base_path(), 0, 0);
    } else {
      res.emplace_back(store.base_path(), svfs.f_bavail * svfs.f_bsize, svfs.f_blocks * svfs.f_bsize);
    }
  }
  return res;
}

auto store_ctx_group_t::next_idx() -> uint16_t {
  return ((*m_idx)++) % m_stores.size();
}

auto store_ctx_group_t::next_file_id() -> uint64_t {
  return (*m_file_id)++;
}

auto store_ctx_group_t::store_idx(uint16_t idx) -> uint16_t {
  return idx % m_stores.size();
}

auto store_ctx_group_t::map_to_store_idx(uint64_t file_id) -> std::optional<uint16_t> {
  auto it = m_store_idx_map.find(file_id);
  if (it == m_store_idx_map.end()) {
    g_log->log_error(std::format("store group '{}' no file, fileid '{}'", m_name, file_id));
    return std::nullopt;
  }
  return it->second;
}
