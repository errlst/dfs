#pragma once
#include <atomic>
#include <fstream>
#include <generator>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

/*
  对于本系统的存储路径，给出以下定义：
  root_path，存储的根路径，如 /home/errlst/dfs/build/base_path/storage_1/hot
  flat_path，扁平路径，如 00/00
  rel_path，相对路径，如 00/00/abc.txt_nIk6cAOx
  abs_path，绝对路径，如 /home/errlst/dfs/build/base_path/storage_1/hot/00/00/abc
  user_file_name，用户上传提供的文件名（包含后缀），如 abc.txt
  store_file_name，实际存储时的文件名（包括随机后缀），如 abc.txt_nIk6cAOx
*/

namespace storage
{

  class store_ctx
  {
  public:
    store_ctx(std::string_view root_path);

    ~store_ctx() = default;

    /**
     * @brief 创建文件
     *
     * @param file_size 文件所需大小
     */
    auto create_file(uint64_t file_id, uint64_t file_size) -> bool;

    /**
     * @brief 创建文件
     *
     * @param rel_path 指定相对路径
     */
    auto create_file(uint64_t file_id, uint64_t file_size, std::string_view rel_path) -> bool;

    /**
     * @brief 文件追加写入，client 上传和 storge 同步均调用此函数
     *
     */
    auto append_data(uint64_t file_id, std::span<char> data) -> bool;

    /**
     * @brief 关闭写入流
     *
     * @param user_file_name 用户提供文件名
     *
     * @return 如果成功，返回最终存储的 root_path + rel_path
     */
    auto close_write_file(uint64_t file_id, std::string_view user_file_name) -> std::optional<std::pair<std::string, std::string>>;

    /**
     * @brief 关闭写入流
     *
     */
    auto close_write_file(uint64_t file_id) -> std::optional<std::pair<std::string, std::string>>;

    /**
     * @brief 打开读取流
     *
     * @return 返回 file_size
     */
    auto open_read_file(uint64_t file_id, std::string_view rel_path) -> std::optional<uint64_t>;

    /**
     * @brief 读取文件内容
     *
     * @return 成功返回文件内容，否则返回 std::nullopt
     */
    auto read_file(uint64_t file_id, uint64_t size) -> std::optional<std::vector<char>>;

    /**
     * @brief 读取文件内容
     *
     * @return 返回实际读取的字节数
     */
    auto read_file(uint64_t file_id, char *dst, uint64_t size) -> std::optional<uint64_t>;

    /**
     * @brief 关闭读取流
     *
     */
    auto close_read_file(uint64_t file_id) -> bool;

    /**
     * @brief 获取剩余可用空间，每隔一定调用次数，都会更新一次缓存
     *
     */
    auto free_space() -> uint64_t;

    /**
     * @brief 获取总空间
     *
     */
    auto total_space() -> uint64_t { return m_disk_total; }

    /**
     * @brief 获取 root_path
     *
     */
    auto root_path() -> std::string { return m_root_path; }

  private:
    /**
     * @brief 获取 ofstream
     *
     * @return <ofs, rel_path>
     */
    auto peek_ofstream(uint64_t file_id) -> std::pair<std::shared_ptr<std::ofstream>, std::string>;

    /**
     * @brief 获取 ofstream 且会移除
     *
     * @return <ofs, rel_path>
     */
    auto pop_ofstream(uint64_t file_id) -> std::pair<std::shared_ptr<std::ofstream>, std::string>;

    /**
     * @brief 获取 ifstream
     *
     */
    auto peek_ifstream(uint64_t file_id) -> std::pair<std::shared_ptr<std::ifstream>, std::string>;

    /**
     * @brief 获取 ifstream 且会移除
     *
     */
    auto pop_ifstream(uint64_t file_id) -> std::pair<std::shared_ptr<std::ifstream>, std::string>;

    /**
     * @brief 创建 ofstream，并填充空洞
     *
     */
    auto create_ofstream(std::string_view rel_path, uint64_t file_size) -> std::shared_ptr<std::ofstream>;

    /**
     * @brief 获取下一个扁平路径
     *
     */
    auto next_flat_path() -> std::string;

    /**
     * @brief 截取扁平路径
     *
     */
    auto flat_of_rel_path(std::string_view rel_path) -> std::string;

    /**
     * @brief 获取一个有效的相对路径（文件不存在即有效
     *
     * @return std::string
     */
    auto valid_rel_path(std::string_view flat_path) -> std::string;

    /**
     * @brief 减少磁盘缓存
     *
     */
    auto reduce_disk_free(uint64_t size) -> void { m_disk_free = std::min(m_disk_free.load(), m_disk_free - size); }

    /**
     * @brief 检查磁盘空间是否够用，且留余 5% 空间
     *
     */
    auto disk_space_is_enough(uint64_t size) -> bool;

  private:
    std::string m_root_path;              // 根路径
    std::atomic_uint16_t m_flat_idx = 0;  // 扁平路径索引
    uint64_t m_disk_total = 0;            // 磁盘总空间
    std::atomic_uint64_t m_disk_free = 0; // 磁盘可用空间

    std::map<uint64_t, std::pair<std::shared_ptr<std::ifstream>, std::string>> m_ifstreams; // <流, 相对路径>
    std::mutex m_ifstreams_mtx;
    std::map<uint64_t, std::pair<std::shared_ptr<std::ofstream>, std::string>> m_ofstreams; // <流, 相对路径>
    std::mutex m_ofstream_mut;
  };

  /**
   * @brief store_ctx_group 的函数调用均是对 store_ctx 的封装，具体说明参考 store_ctx
   *
   */
  class store_ctx_group
  {
  public:
    store_ctx_group(const std::string &name, const std::vector<std::string> &path);

    ~store_ctx_group() = default;

    auto create_file(uint64_t file_size) -> std::optional<uint64_t>;

    auto create_file(uint64_t file_size, std::string_view rel_path) -> std::optional<uint64_t>;

    auto write_file(uint64_t file_id, std::span<char> data) -> bool;

    auto close_write_file(uint64_t file_id, std::string_view user_file_name) -> std::optional<std::pair<std::string, std::string>> { return m_stores[file_id % m_stores.size()]->close_write_file(file_id, user_file_name); }

    auto close_write_file(uint64_t file_id) -> std::optional<std::pair<std::string, std::string>> { return m_stores[file_id % m_stores.size()]->close_write_file(file_id); }

    /**
     * @brief 打开文件
     *
     * @return 返回 <file_id, file_size, abs_path>
     */
    auto open_read_file(std::string_view rel_path) -> std::optional<std::tuple<uint64_t, uint64_t, std::string>>;

    auto read_file(uint64_t file_id, uint64_t size) -> std::optional<std::vector<char>>;

    auto read_file(uint64_t file_id, char *dst, uint64_t size) -> std::optional<uint64_t>;

    auto close_read_file(uint64_t file_id) -> bool { return m_stores[file_id % m_stores.size()]->close_read_file(file_id); }

    /**
     * @brief 最大空闲空间
     *
     * @return uint64_t
     */
    auto max_free_space() -> uint64_t;

    /**
     * @brief 获取根路径
     *
     */
    auto root_path(uint64_t file_id) -> std::string { return m_stores[file_id % m_stores.size()]->root_path(); }

    /**
     * @brief 获取 stores
     *
     */
    auto stores() -> std::vector<std::shared_ptr<store_ctx>> { return m_stores; }

  private:
    /**
     * @brief 从 idx 开始遍历所有 store
     *
     * @return storage 和 file_id
     */
    auto iterate_store(uint64_t start_idx) -> std::generator<std::pair<std::shared_ptr<store_ctx>, uint64_t>>;

  private:
    /* 组名，如 "hot_storgae_group" 、"cold_storage_group" */
    std::string m_name;

    /* 组中的 stores */
    std::vector<std::shared_ptr<store_ctx>> m_stores = {};

    /* 表达式 `idx % m_stores.size()` 可以获取具体的 store，对于 store，可以通过 idx 获取具体的文件 */
    std::atomic_uint64_t m_store_idx = 0;
  };
} // namespace storage
