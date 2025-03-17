#include "protocol.h"
#include "enum.h"
#include <utility>

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
  *frame = {.cmd = cmd, .type = type, .stat = stat, .data_len = data_len};
  return frame;
}

auto proto_frame_to_string(const proto_frame &frame) -> std::string {
  return std::format("{{ magic: {:x}, id: {}, cmd: {}({}), type: {}, stat: {}, data_len: {} }}",
                     frame.magic, frame.id, enum_name(frame.cmd), std::to_underlying(frame.cmd),
                     enum_name(frame.type), frame.stat, frame.data_len);
}

} // namespace common
