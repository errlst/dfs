#include "storage_signal.h"
#include "common/log.h"
#include "common/signals.h"

#define SIG_QUIT SIGINT
#define SIG_SYNC SIGUSR1
#define SIG_MIGRATE SIGUSR2

static auto valid_signals = std::map<std::string, int>{
    {"quit", SIG_QUIT},
    {"sync", SIG_SYNC},
    {"migrate", SIG_MIGRATE},
};

static auto sig_handle(int signo, siginfo_t *siginfo, void *) -> void {
  switch (signo) {
    case SIG_QUIT:
      break;
    case SIG_SYNC:
      break;
    case SIG_MIGRATE:
      break;
    default:
      LOG_ERROR("recv unhandled signo {}", signo);
  }
}

auto init_signal() -> bool {
  auto signals = std::vector<common::signal_>{};
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
