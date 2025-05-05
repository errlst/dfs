#pragma once
#include <common/signal.h>
#include <map>

#define SIG_QUIT SIGINT
#define SIG_SYNC SIGUSR1
#define SIG_MIGRATE SIGUSR2

namespace storage_detail
{

  inline auto valid_signals = std::map<std::string, int>{
      {"quit", SIG_QUIT},
      {"sync", SIG_SYNC},
      {"migrate", SIG_MIGRATE},
  };

  auto signal_handle(int signo, siginfo_t *siginfo, void *) -> void;

} // namespace storage_detail

namespace storage
{

  /**
   * @brief 初始化信号处理程序
   *
   */
  auto init_signal() -> bool;

  /**
   * @brief 处理信号
   *
   */
  auto signal_process(std::string_view sig, int pid) -> void;

} // namespace storage
