#include "config.h"
#include "server.h"
#include "signal.h"
#include <common/exception.h>
#include <common/pid.h>
#include <common/util.h>
#include <print>
#include <string_view>

auto show_usage() -> void
{
  std::println("usgae: storage [options]");
  std::println("options:");
  std::println("  -h  show this help message and exit");
  std::println("  -c  specify the configuration file");
  std::println("  -d  run as a daemon");
  std::println("  -s  send signals");
  std::println("      quit");
  std::println("      sync");
  std::println("      migrate");
  exit(-1);
}

auto main(int argc, char *argv[]) -> int
{
  auto config_path = std::string{};
  auto send_signal = false;
  auto signal_to_send = std::string{};
  auto force = false;
  for (auto i = 1; i < argc; ++i)
  {
    auto flag = std::string_view{argv[i]};
    if (flag == "-c")
    {
      if (i + 1 >= argc)
      {
        show_usage();
      }
      config_path = argv[++i];
    }
    else if (flag == "-s")
    {
      if (i + 1 >= argc)
      {
        show_usage();
      }
      send_signal = true;
      signal_to_send = argv[++i];
    }
    else if (flag == "-f")
    {
      force = true;
    }
    else
    {
      show_usage();
    }
  }

  storage::init_config(config_path);

  if (send_signal)
  {
    storage::signal_process(signal_to_send, common::read_pid_file(storage::storage_config.common.base_path));
    return 0;
  }

  common::init_base_path(storage::storage_config.common.base_path);
  common::init_log(storage::storage_config.common.base_path, false, static_cast<common::log_level>(storage::storage_config.common.log_level));
  common::write_pid_file("storage", storage::storage_config.common.base_path, force);
  storage::init_signal();

  auto io = asio::io_context{};
  auto gurad = asio::make_work_guard(io);

  asio::co_spawn(io, storage::storage_server(), common::exception_handle);

  auto thread_count = storage::storage_config.common.thread_count;
  for (auto i = 0u; i < thread_count - 1; ++i)
  {
    std::thread{[&]
                {
                  io.run();
                }}
        .detach();
  }
  return io.run();
}
