#include "storage_signal.h"
#include "common/log.h"
#include "common/signals.h"
#include "migrate_service.h"
#include "sync_service.h"

#define SIG_QUIT SIGINT
#define SIG_SYNC SIGUSR1
#define SIG_MIGRATE SIGUSR2

static auto valid_signals = std::map<std::string, int>{
    {"quit", SIG_QUIT},
    {"sync", SIG_SYNC},
    {"migrate", SIG_MIGRATE},
};

static auto sig_handle(int signo, siginfo_t *siginfo, void *) -> void {
  LOG_INFO("recv signal {} from {}", signo, siginfo->si_pid);
  switch (signo) {
    case SIG_QUIT:
      quit_storage_service();
      break;
    case SIG_SYNC:
      start_sync_service();
      break;
    case SIG_MIGRATE:
      start_migrate_service();
      break;
    default:
      LOG_ERROR("recv unhandled signo {}", signo);
  }
}

auto init_storage_signal() -> bool {
  auto signals = std::vector<common::signal_t>{};
  for (const auto &[name, sig] : valid_signals) {
    signals.push_back({sig, name, sig_handle});
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
