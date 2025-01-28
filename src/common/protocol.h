#pragma once
#include <cstdint>

constexpr uint16_t FRAME_MAGIC = 0x55aa;

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
        如果成功，master 返回同组的其他 storage
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
};

struct proto_frame_t {
    uint32_t frame_magic : 16 = FRAME_MAGIC;
    uint8_t id; // 同一个 cmd 的请求和响应的 id 一致
    uint8_t cmd;
    uint8_t stat = 0;           // 状态码
    uint32_t data_len : 24 = 0; // data 的长度
    char data[0];
};
static_assert(sizeof(proto_frame_t) == 8);
