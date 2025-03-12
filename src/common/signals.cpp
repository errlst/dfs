#include "signals.h"
#include "log.h"

auto common::init_signal(std::vector<signal_t> sigs) -> bool {
  struct sigaction sa;
  for (auto &sig : sigs) {
    std::memset(&sa, 0, sizeof(sa));
    if (sig.handler) {
      sa.sa_sigaction = sig.handler;
      sa.sa_flags = SA_SIGINFO;
    } else {
      sa.sa_handler = SIG_IGN;
    }

    sigemptyset(&sa.sa_mask);
    if (sigaction(sig.signo, &sa, nullptr) != 0) {
      LOG_CRITICAL("sigaction {} failed, {}", sig.name, strerror(errno));
      return false;
    }
  }

  return true;
}

auto common::send_signal(int pid, int signo) -> void {
  if (kill(pid, signo) != 0) {
    LOG_ERROR("send signal {} to {} failed, {}", signo, pid, strerror(errno));
  }
}