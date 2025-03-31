#pragma once

#include "store.h"
#include <common/connection.h>
#include <cstdint>
#include <string>

namespace storage {

  /**
   * @brief 连接类型
   *
   */
  enum class conn_type_t {
    client,
    storage,
    master,
  };

  /**
   * @brief 连接需要的额外数据
   *
   */
  enum conn_data : uint64_t {
    conn_type,

    client_upload_file_id,

    /* 普通分块下载 */
    client_download_file_id,
    client_download_store_group,

    /* 零拷贝下载 */
    client_download_file_path,
    client_download_file_size,

    storage_sync_upload_file_id,
  };

  using client_upload_file_id_t = uint64_t;
  using client_download_file_id_t = uint64_t;
  using client_download_store_group_t = std::shared_ptr<store_ctx_group>;
  using client_download_file_path_t = std::string;
  using client_download_file_size_t = uint64_t;
  using storage_sync_upload_file_id_t = uint64_t;

  using request_handle_t = std::function<asio::awaitable<bool>(common::proto_frame_ptr, common::connection_ptr)>;
#define REQUEST_HANDLE_PARAMS common::proto_frame_ptr request, common::connection_ptr conn

} // namespace storage