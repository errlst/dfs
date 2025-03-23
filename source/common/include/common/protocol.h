#pragma once

#include <memory>
#include <utility>

namespace common {

  /**
   * @brief xx_??? 的命令为通用命令
   *        sm_??? 的命令为 storage 发送给 master 的命令
   *        ss_??? 的命令为 storage 发送给 storage 的命令
   *        cs_??? 的命令为 client 发送给 storage 的命令
   *
   *        如果后面的注释没有提及 request 或 response，则代表 request 或 resposne 无需 payload
   */
  enum class proto_cmd : uint16_t {
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
    ss_upload_sync_start,

    /**
     * @brief 发送同步文件的数据。
     *
     * @param request { array data }。stat == STAT_FINISH 表示同步完成
     */
    ss_upload_sync,

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
    cs_upload_start,

    /**
     * @brief 上传数据（不能并行上传多个数据块）
     *
     * @param request { array data }。stat == STAT_FINISH 表示上传完成
     */
    cs_upload,

    /**
     * @brief 开始下载文件
     *
     * @param request   { string rel_path }
     * @param response  { uint64 filesize }
     */
    cs_download_start,

    /**
     * @brief 下载数据
     *
     * @param response  { array data }。stat == STAT_FINISH 表示下载完成
     */
    cs_download,

    sentinel,
  };

  /**
   * @brief frame 类型
   *
   */
  enum class frame_type : uint8_t {
    request,
    response,
    sentinel,
  };

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

  constexpr auto FRAME_MAGIC = uint16_t{0x55aa};
  constexpr auto FRAME_STAT_OK = uint8_t{0};
  constexpr auto FRAME_STAT_FINISH = uint8_t{255};

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
    proto_cmd cmd;
    frame_type type;
    uint8_t stat = 0;
    uint32_t data_len = 0;
    char data[0];
  };
  static_assert(sizeof(proto_frame) == 12);

  using proto_frame_ptr = std::shared_ptr<proto_frame>;

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
  auto create_frame(proto_cmd cmd, frame_type type, uint32_t data_len, uint8_t stat = FRAME_STAT_OK) -> std::shared_ptr<proto_frame>;

} // namespace common

/************************************************************** */
/* proto_frame 格式化字符串 */
/************************************************************** */

template <auto value>
static constexpr auto enum_name() {
  auto name = std::string_view{__PRETTY_FUNCTION__};
  auto start = name.find('=') + 2;
  auto end = name.size() - 1;
  name = std::string_view{name.data() + start, end - start};
  start = name.rfind("::");
  return start == std::string_view::npos ? name : std::string_view{name.data() + start + 2, name.size() - start - 2};
}

template <typename Enum>
  requires std::is_enum_v<Enum>
static constexpr auto enum_name(Enum value) -> std::string_view {
  constexpr static auto names = []<std::size_t... Is>(std::index_sequence<Is...>) {
    return std::array<std::string_view, std::to_underlying(Enum::sentinel)>{
        enum_name<static_cast<Enum>(Is)>()...};
  }(std::make_index_sequence<std::to_underlying(Enum::sentinel)>{});

  if (value < Enum::sentinel)
    return names[static_cast<std::size_t>(value)];
  else
    return "unknown";
}

template <>
struct std::formatter<common::proto_frame> {
  constexpr auto parse(std::format_parse_context &ctx) {
    return ctx.begin();
  }

  auto format(const common::proto_frame &frame, std::format_context &ctx) const {
    return std::format_to(
        ctx.out(),
        "{{ magic: {:x}, id: {}, cmd: {}({}), type: {}, stat: {}, data_len: {} }}",
        frame.magic,
        frame.id,
        enum_name(frame.cmd),
        std::to_underlying(frame.cmd),
        enum_name(frame.type),
        frame.stat,
        frame.data_len);
  }
};