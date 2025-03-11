#pragma once

#include <signal.h> // IWYU pragma: keep
#include <string>
#include <vector>

namespace common {
struct signal_ {
  int signo;
  std::string name;
  void (*handler)(int signo, siginfo_t *siginfo, void *ucontext);
};

auto init_signal(std::vector<signal_> sigs) -> bool;

auto send_signal(int pid, int signo) -> void;
} // namespace common