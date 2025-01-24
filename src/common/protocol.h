#pragma once
#include <cstdint>
#include <memory>

constexpr uint16_t frame_valid_magic = 0x55aa;

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
    a_heart = 0, // 心跳

    /*
        storage 注册到 master
    */
    sm_regist,

    /*
        新的 storage 注册到组
    */
    ms_new_regist,

    /*
        获取最大可使用剩余空间，只在 hot_store 中搜索
    */
    ms_max_free_space,

    /*
        client 转为 storage
    */
    ss_as_partner,

    /*
        获取合适的 storage，用于创建文件
    */
    cm_create_file,

    /*
        创建文件
    */
    cs_create_file,

    /*
        增加文件内容
    */
    cs_append_file,

    /*
        关闭文件并重命名
    */
    cs_close_file,
};

struct proto_frame_t {
    uint32_t magic : 16 = frame_valid_magic;
    uint8_t id; // 同一个 cmd 的请求和响应的 id 一致
    uint8_t cmd;
    uint8_t stat = 0;      // 状态码
    uint32_t len : 24 = 0; // data 的长度
    char data[0];
};
static_assert(sizeof(proto_frame_t) == 8);

using proto_frame_ptr_t = std::shared_ptr<proto_frame_t>;

struct sm_regist_req_t {
    uint32_t storage_id;
    uint32_t storage_magic;
    uint32_t master_magic; // master 的 magic
    uint16_t port;
    char ip[];
};

struct ms_new_regist_req_t {
    uint32_t storage_id;
    uint32_t magic;
    uint16_t port;
    char ip[0];
};

struct ss_as_partner_req_t {
    uint32_t id;
    uint32_t magic;
};
