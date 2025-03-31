#include "signal.h"
#include "migrate.h"
#include "sync.h"
#include <common/log.h>

namespace storage_detail {

  using namespace storage;

  auto signal_handle(int signo, siginfo_t *siginfo, void *) -> void {
    LOG_INFO("recv signal {} from {}", signo, siginfo->si_pid);
    switch (signo) {
      case SIG_QUIT:
        // quit_storage_service();
        break;
      case SIG_SYNC:
        trigger_sync_service();
        break;
      case SIG_MIGRATE:
        trigger_migrate_service();
        break;
      default:
        LOG_ERROR("recv unhandled signo {}", signo);
    }
  }

} // namespace storage_detail

namespace storage {

  using namespace storage_detail;

  auto init_signal() -> bool {
    auto signals = std::vector<common::signal_t>{};
    for (const auto &[name, sig] : valid_signals) {
      signals.push_back({sig, name, signal_handle});
    }
    return common::init_signal(signals);
  }

  auto signal_process(std::string_view sig, int pid) -> void {
    if (auto it = valid_signals.find(std::string{sig}); it != valid_signals.end()) {
      common::send_signal(pid, it->second);
      LOG_INFO("send signal {} to {} suc", sig, pid);
    } else {
      LOG_ERROR("invalid signal {}", sig);
    }
  }

} // namespace storage
