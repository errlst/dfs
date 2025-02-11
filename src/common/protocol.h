#pragma once
#include <cstdint>
#include <netinet/in.h>

namespace common {
enum proto_cmd : uint16_t {
  /* 心跳包 */
  xx_heart_ping,

  /* 建立心跳
    request xx_heart_establish_request
  */
  xx_heart_establish,

  /*
    storage 注册到 master
    request: sm_regist_request
    response: sm_regist_response
  */
  sm_regist,

  /*
    storage 注册到 storage
    request: ss_regist_request
    response: void
  */
  ss_regist,

  /*
    开始同步上传的文件
    request: uint64 filesize, string filepath   xx/xx/<filename>_<file_suffix>
    response: void
  */
  ss_upload_sync_open,

  /*
    同步数据
    request: array data   长度为0表示同步完成
    response: void
  */
  ss_upload_sync_append,

  /*
    获取空闲内存
    request: void
    response: uint64 free_space
  */
  ms_get_free_space,

  /*
    获取可用的 storage
    request: uint64 need_space
    response: cm_fetch_one_storage_response
  */
  cm_fetch_one_storage,

  /*
    获取一组 storage
    request: uint32 group_id
    response: cm_fetch_group_storages_response
  */
  cm_fetch_group_storages,

  /*
    开始上传文件
    request: uint64 filesize
    response: void
  */
  cs_upload_open,

  /*
    上传数据
    request: array data 如果长度为0，则终止上传
    response: void
  */
  cs_upload_append,

  /*
    结束上传
    request: string filename
    response: string filepath  <group>/xx/xx/<filename>_<suffix>
  */
  cs_upload_close,

  /*
    开始下载文件
    request: string filepath  xx/xx/<filename>_<suffix>
    response: u64 filesize
  */
  cs_download_open,

  /*
    下载文件
    request: void
    response: array data  长度为0表示下载完成
  */
  cs_download_append,

};

struct xx_heart_establish_request {
  uint32_t timeout;
  uint32_t interval;
};

constexpr auto FRAME_MAGIC = (uint16_t)0x55aa;
constexpr auto REQUEST_FRAME = 0;
constexpr auto RESPONSE_FRAME = 1;

#pragma pack(push, 1)
struct proto_frame {
  uint16_t magic = FRAME_MAGIC;
  uint16_t id;
  uint16_t cmd;
  uint8_t type; // request or response
  uint8_t stat = 0;
  uint32_t data_len = 0;
  char data[0];
};
#pragma pack(pop)

inline auto trans_frame_to_net(proto_frame *frame) -> void {
  frame->magic = htons(frame->magic);
  frame->id = htons(frame->id);
  frame->cmd = htons(frame->cmd);
  frame->data_len = htonl(frame->data_len);
}
inline auto trans_frame_to_host(proto_frame *frame) -> void {
  frame->magic = ntohs(frame->magic);
  frame->id = ntohs(frame->id);
  frame->cmd = ntohs(frame->cmd);
  frame->data_len = ntohl(frame->data_len);
}
} // namespace common
