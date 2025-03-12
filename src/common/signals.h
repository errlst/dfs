#pragma once

#include <signal.h> // IWYU pragma: keep
#include <string>
#include <vector>

namespace common {
struct signal_t {
  int signo;
  std::string name;
  void (*handler)(int signo, siginfo_t *siginfo, void *ucontext);
};

auto init_signal(std::vector<signal_t> sigs) -> bool;

auto send_signal(int pid, int signo) -> void;
} // namespace common