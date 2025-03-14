#pragma once
#include "./enum.h"
#include <cstdint>
#include <memory>
#include <netinet/in.h>

namespace common {

/**
 * @brief xx_??? 的命令为通用命令
 *        sm_??? 的命令为 storage 发送给 master 的命令
 *        ss_??? 的命令为 storage 发送给 storage 的命令
 *        cs_??? 的命令为 client 发送给 storage 的命令
 *
 *        如果后面的注释没有提及 request 或 response，则代表 request 或 resposne 无需 payload
 */
ENUM_MACRO(
    proto_cmd,

    /**
     * @brief 建立心跳，由服务端发起
     *
     * @param request   xx_heart_establish_request
     */
    xx_heart_establish,

    /**
     * @brief 发送一次心跳，两端均只发送 ping 心跳，无需 pong 响应，一段时间内没有收到任何消息即视为断连
     *
     */
    xx_heart_ping,

    /**
     * @brief 注册到 master
     *
     * @param request   proto::sm_regist_request
     * @param response  proto::sm_regist_response
     */
    sm_regist,

    /**
     * @brief 注册到同组的 storage
     *
     * @param request   proto::ss_regist_request
     */
    ss_regist,

    /**
     * @brief 开始同步上传的文件（多个文件同步的过程是同步进行的，而非并行）
     *
     * @param request     { uint64 filesize, string relpath }
     */
    ss_upload_sync_open,

    /**
     * @brief 发送同步文件的数据，发送请求时，data_len==0 或者 stat==255 均表示同步完成
     *
     * @param request { array data }
     */
    ss_upload_sync_append,

    /**
     * @brief 获取 storage 最大可用内存
     *
     * @param response  { uint64 size }
     */
    ms_get_max_free_space,

    /**
     * @brief 获取性能监控指标
     *
     * @param response  { string json_str }
     */
    ms_get_metrics,

    /**
     * @brief 获取一个可用的 storage
     *
     * @param request   { uint64 needspace }
     * @param response  proto::cm_fetch_one_storage_response
     */
    cm_fetch_one_storage,

    /**
     * @brief 获取一组 storages
     *
     * @param request   { uint32 groupid }
     * @param response  proto::cm_fetch_group_storages_response
     */
    cm_fetch_group_storages,

    /**
     * @brief 开始上传文件（不能并行上传多个文件）
     *
     * @param request { uint64 filesize }
     */
    cs_upload_open,

    /**
     * @brief 上传数据（不能并行上传多个数据块）
     *
     * @param request { array data }  如果长度为0，则表示异常需要中止上传
     */
    cs_upload_append,

    /**
     * @brief 上传完成
     *
     * @param request   { string user_file_name }
     * @param response  { string final_file_path }  <group_id>/<rel_path>
     */
    cs_upload_close,

    /**
     * @brief 开始下载文件
     *
     * @param request   { string rel_path }
     * @param response  { uint64 filesize }
     */
    cs_download_open,

    /**
     * @brief 下载数据
     *
     * @param response  { array data }  长度为 0 表示下载完成
     */
    cs_download_append);

/**
 * @brief 建立心跳时的 payload
 *
 * @param timeout   心跳超时时长，单位毫秒
 * @param interval  心跳发送间隔，单位毫秒
 */
struct xx_heart_establish_request {
  uint32_t timeout;
  uint32_t interval;
};

#define FRAME_MAGIC (uint16_t)0x55aa

ENUM_MACRO(proto_type, request, response);

/**
 * @brief 协议帧头
 *
 * @param magic     魔数
 * @param id        帧 id，一个请求对应一个响应
 * @param cmd       命令 proto_cmd
 * @param type      请求或响应
 * @param stat      状态，只用于响应
 * @param data_len  payload 长度
 * @param data      payload
 */
struct proto_frame {
  uint16_t magic = FRAME_MAGIC;
  uint16_t id;
  uint16_t cmd;
  uint8_t type;
  uint8_t stat = 0;
  uint32_t data_len = 0;
  char data[0];
};
static_assert(sizeof(proto_frame) == 12);

/**
 * @brief frame 的字节序转换
 *
 */
auto trans_frame_to_net(proto_frame *frame) -> void;
auto trans_frame_to_host(proto_frame *frame) -> void;

/**
 * @brief 构造 frame
 *
 */
auto create_request_frame(uint16_t cmd, uint32_t data_len) -> std::shared_ptr<proto_frame>;
// auto create_response_frame() -> std::shared_ptr<proto_frame>;

/**
 * @brief 转换字符串
 *
 */
auto proto_frame_to_string(const proto_frame &frame) -> std::string;
} // namespace common
