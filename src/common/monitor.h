#include <cstdint>
#include <string>
#include <vector>

struct cpu_usage_t {
    uint64_t user;
    uint64_t nice;
    uint64_t sys;
    uint64_t idle;
    uint64_t io_wait;
    uint64_t hard_irq;
    uint64_t soft_irq;
    uint64_t steal;
    uint64_t guest;
    uint64_t guest_nice;
};

/* 不包括中断相关统计 */
struct proc_stat_t {
    std::vector<cpu_usage_t> cpu_usage;
    uint64_t ctxt;
    uint64_t btime;
    uint64_t processes;
    uint64_t procs_running;
    uint64_t procs_blocked;
};

struct proc_pid_stat_t {
    int64_t pid;
    std::string comm;
    char state;
    int64_t ppid;
    int64_t pgrp;
    int64_t session;
    int64_t tty_nr;
    int64_t tpgid;
    int64_t flags;
    int64_t minflt;
    int64_t cminflt;
    int64_t majflt;
    int64_t cmajflt;
    int64_t utime;
    int64_t stime;
    int64_t cutime;
    int64_t cstime;
    int64_t priority;
    int64_t nice;
    int64_t num_threads;
    int64_t itrealvalue;
    int64_t starttime;
    int64_t vsize;
    int64_t rss;
    int64_t rsslim;
    int64_t startcode;
    int64_t endcode;
    int64_t startstack;
    int64_t kstkesp;
    int64_t kstkeip;
    int64_t signal;
    int64_t blocked;
    int64_t sigignore;
    int64_t sigcatch;
    int64_t wchan;
    int64_t nswap;
    int64_t cnswap;
    int64_t exit_signal;
    int64_t processor;
    int64_t rt_priority;
    int64_t policy;
    int64_t delayacct_blkio_ticks;
    int64_t guest_time;
    int64_t cguest_time;
};

auto monitor_proc_stat() -> proc_stat_t;

auto monitor_proc_pid_stat(int pid) -> proc_pid_stat_t;

/* 磁盘空间 <free, total> */
auto monitor_fs_usage(const std::string &path) -> std::tuple<uint64_t, uint64_t>;