// #include "./monitor_service.h"
// #include "../common/monitor.h"
// #include "./global.h"

// struct {
//     int pid;
//     std::string filename;
//     std::chrono::milliseconds interval;
//     std::vector<std::tuple<std::string, uint64_t, uint64_t>> hot_fs;
//     std::vector<std::tuple<std::string, uint64_t, uint64_t>> warm_fs;
//     std::vector<std::tuple<std::string, uint64_t, uint64_t>> cold_fs;
// } static conf;

// static monitor_data_t monitor_data;

// static uint64_t sys_cpu_jfffies_delat = 0;
// static uint64_t prev_sys_cpu_jiffies = 0;
// static uint64_t prev_sys_cpu_idle_jiffies = 0;

// static uint64_t proc_cpu_jiffies_delta = 0;
// static uint64_t prev_proc_cpu_jiffies = 0;

// /* /proc/stat */
// static auto slove_proc_stat() -> asio::awaitable<void> {
//     auto proc_stat = monitor_proc_stat();

//     /* sys cpu usage*/
//     auto tmp_span = std::span<uint64_t>{reinterpret_cast<uint64_t *>(&proc_stat.cpu_usage[0]), 10};
//     auto sys_cpu_jiffies = std::accumulate(tmp_span.begin(), tmp_span.end(), (uint64_t)0);
//     sys_cpu_jfffies_delat = sys_cpu_jiffies - prev_sys_cpu_jiffies;
//     monitor_data.sys_cpu_usage = 1.f - 1.f * (proc_stat.cpu_usage[0].idle - prev_sys_cpu_idle_jiffies) / (sys_cpu_jfffies_delat);
//     prev_sys_cpu_jiffies = sys_cpu_jiffies;
//     prev_sys_cpu_idle_jiffies = proc_stat.cpu_usage[0].idle;
//     co_return;
// }

// /* proc/pid/stat */
// static auto slove_proc_pid_stat() -> asio::awaitable<void> {
//     auto proc_pid_stat = monitor_proc_pid_stat(conf.pid);

//     /* proc cpu usage */
//     auto proc_cpu_jiffies = proc_pid_stat.utime + proc_pid_stat.stime;
//     proc_cpu_jiffies_delta = proc_cpu_jiffies - prev_proc_cpu_jiffies;
//     monitor_data.proc_cpu_usage = 1.f * proc_cpu_jiffies_delta / sys_cpu_jfffies_delat;
//     prev_proc_cpu_jiffies = proc_cpu_jiffies;
//     co_return;
// }

// static auto slove_fs_usage() -> asio::awaitable<void> {
//     for (auto &[path, free, total] : conf.hot_fs) {
//         auto [_free, _total] = monitor_fs_usage(path);
//         if (_total) {
//             free = _free;
//             total = _total;
//         }
//     }
//     for (auto &[path, free, total] : conf.warm_fs) {
//         auto [_free, _total] = monitor_fs_usage(path);
//         if (_total) {
//             free = _free;
//             total = _total;
//         }
//     }
//     for (auto &[path, free, total] : conf.cold_fs) {
//         auto [_free, _total] = monitor_fs_usage(path);
//         if (_total) {
//             free = _free;
//             total = _total;
//         }
//     }
//     co_return;
// }

// static auto to_file() -> asio::awaitable<void> {
//     auto ofs = std::ofstream{conf.filename};
//     ofs << std::format("pid: {}\n", conf.pid);
//     ofs << std::format("sys_cpu_usage: {:.2f}%\n", monitor_data.sys_cpu_usage * 100.f);
//     ofs << std::format("proc_cpu_usage: {:.2f}%\n", monitor_data.proc_cpu_usage * 100.f);

// #define FS_TO_FILE(name, fs)                                                                                                      \
//     ofs << name ":\n";                                                                                                            \
//     for (const auto &[path, free, total] : fs) {                                                                                  \
//         ofs << std::format("\t{}: {:.2f}GB / {:.2f}GB\n", path, 1. * free / 1024 / 1024 / 1024, 1. * total / 1024 / 1024 / 1024); \
//     }
//     FS_TO_FILE("hot storage", conf.hot_fs);
//     FS_TO_FILE("warm storage", conf.warm_fs);
//     FS_TO_FILE("cold storage", conf.cold_fs);
// #undef FS_TO_FILE

//     co_return;
// }

// static auto init_conf() -> asio::awaitable<void> {
//     conf.pid = getpid();
//     conf.filename = std::format("{}/data/monitor.txt", g_m_conf["common"]["base_path"].get<std::string>());
//     conf.interval = std::chrono::milliseconds{g_m_conf["monitor_service"]["interval"].get<uint64_t>()};
//     for (auto &path : g_m_conf["storage_service"]["hot_paths"]) {
//         conf.hot_fs.emplace_back(path.get<std::string>(), 0, 0);
//     }
//     for (auto &path : g_m_conf["storage_service"]["warm_paths"]) {
//         conf.warm_fs.emplace_back(path.get<std::string>(), 0, 0);
//     }
//     for (auto &path : g_m_conf["storage_service"]["cold_paths"]) {
//         conf.cold_fs.emplace_back(path.get<std::string>(), 0, 0);
//     }

//     co_return;
// }

// auto monitor_service() -> asio::awaitable<void> {
//     g_m_log->log_info("monitor service start");
//     co_await init_conf();
//     auto timer = asio::steady_timer{co_await asio::this_coro::executor};
//     while (true) {
//         co_await slove_proc_stat();
//         co_await slove_proc_pid_stat();
//         co_await slove_fs_usage();
//         co_await to_file();
//         timer.expires_after(conf.interval);
//         co_await timer.async_wait(asio::use_awaitable);
//     }

//     co_return;
// }