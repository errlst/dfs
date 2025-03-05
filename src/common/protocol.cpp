#include "./protocol.h"

namespace common {
auto trans_frame_to_net(proto_frame *frame) -> void {
  frame->magic = htons(frame->magic);
  frame->id = htons(frame->id);
  frame->cmd = htons(frame->cmd);
  frame->data_len = htonl(frame->data_len);
}

auto trans_frame_to_host(proto_frame *frame) -> void { trans_frame_to_net(frame); }

auto create_request_frame(uint16_t cmd, uint32_t data_len) -> std::shared_ptr<proto_frame> {
  auto frame = std::shared_ptr<proto_frame>{(proto_frame *)malloc(sizeof(proto_frame) + data_len), free};
  *frame = {.cmd = cmd, .data_len = data_len};
  return frame;
}

auto proto_frame_to_string(const proto_frame &frame) -> std::string {
  return std::format("{{ magic: {:x}, id: {}, cmd: {}({}), type: {}, stat: {}, data_len: {} }}",
                     frame.magic, frame.id, proto_cmd_to_string(frame.cmd), frame.cmd, proto_type_to_string(frame.type), frame.stat, frame.data_len);
}

} // namespace common
