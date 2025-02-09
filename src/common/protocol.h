#pragma once
#include <cstdint>

/*
    所有 cmd 为指定两方使用
        a all 任意两方
        m master
        s storage
        c client
        ms -> master to storage

    所有协议都有对应的 xx_cmd_req_t 和 xx_cmd_res_t（心跳不需要）
*/
enum class proto_cmd_e : uint8_t {
  /*
    心跳
  */
  a_heart = 0,

  /*
    建立心跳
    request_t ： {uint32_t timeout, uint32_t interval}
    response_t : { }
  */
  a_establish_heart,

  /*
    storage 注册到 master
    如果成功，master 返回 group_id 和同组的其他 storage
    request_t & response_t : sm_regist.proto
  */
  sm_regist,

  /*
    连接到同组的 storage
    request_t : { uint32_t storage_magic }
    response_t : { }
  */
  ss_regist,

  /*
    获取 storage 最大可使用存储（hot 的大小
    request_t : { }
    response_t : { uint64_t useable_disk }
  */
  ms_fs_free_size,

  /*
    获取一个有效的 storage（通过需要的 size 和缓存的 storage 信息进行比较，最后返回的 storage 大概率是有效的 storage
    request_t : { uint64_t size }
    response_t : cm_valid_storage.proto
  */
  cm_valid_storage,

  /*
    创建文件，创建文件后就可以直接上传文件，直到关闭文件
    request_t: { uint64_t size }
    response_t  : { }
  */
  cs_create_file,

  /*
    上传文件
    request_t : { char data[] }
    response_t : { }
  */
  cs_upload_file,

  /*
    中断上传（ storage 会删除文件
    request_t : { }
    response_t : { }
  */
  cs_abort_upload,

  /*
    结束上传
    request_t : { char filename[] }
    response_t : { char filepath[] }    最终返回的文件名会会附带一个8字节后缀，最终返回的路径为 <group_id>/xx/xx/<file_name>_<suffix>
  */
  cs_close_file,

  /*
    同步上传文件
    request_t : { uint64_t filesize; char filename[] }
    response_t : { }
  */
  ss_sync_upload_open,

  /*
    追加数据
    request_t : { char data[] } 长度为0表示上传完成
    response_t : { }
  */
  ss_sync_upload_append,

  /*
    获取组中的所有 storage
    request_t : { uint32_t group_id }
    response_t : cm_group_storages.proto
  */
  cm_group_storages,

  /*
    打开文件
    request_t : { char filename[] }  filename 格式为 xx/xx/<file_name>，不包含组号
    response_t : { uint64_t file_size }
  */
  cs_open_file,

  /*
    下载文件
    request_t : { uint32_t size }   size 为本次请求的数据大小，范围必须在 [1, 2^24-1]
    response_t : { char data[] }    如果长度为0，则表示下载完成
  */
  cs_download_file,

};

constexpr uint16_t FRAME_MAGIC = 0x55aa;

struct proto_frame_t {
  uint16_t magic = FRAME_MAGIC;
  uint16_t id;
  uint16_t cmd;
  uint16_t stat = 0;
  uint32_t data_len;
  char data[0];
};