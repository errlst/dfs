module;

#include <memory>
#include <netinet/in.h>
#include <string>
#include <utility>

module common.protocol;

namespace common {

  auto trans_frame_to_net(proto_frame *frame) -> void {
    frame->magic = htons(frame->magic);
    frame->id = htons(frame->id);
    frame->cmd = static_cast<proto_cmd>(htons(std::to_underlying(frame->cmd)));
    frame->data_len = htonl(frame->data_len);
  }

  auto trans_frame_to_host(proto_frame *frame) -> void { trans_frame_to_net(frame); }

  auto create_frame(proto_cmd cmd, frame_type type, uint32_t data_len, uint8_t stat) -> std::shared_ptr<proto_frame> {
    auto frame = std::shared_ptr<proto_frame>{(proto_frame *)malloc(sizeof(proto_frame) + data_len), free};
    *frame = {
        .cmd = cmd,
        .type = type,
        .stat = stat,
        .data_len = data_len,
    };
    return frame;
  }

} // namespace common