#include "./protocol.h"

namespace common {
auto trans_frame_to_net(proto_frame *frame) -> void {
  frame->magic = htons(frame->magic);
  frame->id = htons(frame->id);
  frame->cmd = htons(frame->cmd);
  frame->data_len = htonl(frame->data_len);
}

auto trans_frame_to_host(proto_frame *frame) -> void { trans_frame_to_net(frame); }
} // namespace common
