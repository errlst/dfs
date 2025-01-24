#include "monitor.h"

#include <fstream>
#include <sstream>
#include <sys/statvfs.h>

static auto read_file(std::string_view file) -> std::string {
    if (auto ifs = std::ifstream{file.data()}; ifs.is_open()) {
        return std::string{std::istreambuf_iterator<char>{ifs}, std::istreambuf_iterator<char>{}};
    }
    return "";
}

auto monitor_proc_stat() -> proc_stat_t {
    auto content = read_file("/proc/stat");
    if (content.empty()) {
        return {};
    }

    auto proc_stat = proc_stat_t{};
    auto iss = std::istringstream{content};
    auto line = std::string{};
    while (std::getline(iss, line)) {
        if (line.starts_with("cpu")) {
            auto cpu_usage = cpu_usage_t{};
            auto iss = std::istringstream{line.substr(line.find_first_of(' '))};
            iss >> cpu_usage.user >> cpu_usage.nice >> cpu_usage.sys >> cpu_usage.idle >> cpu_usage.io_wait >> cpu_usage.hard_irq >> cpu_usage.soft_irq >> cpu_usage.steal >> cpu_usage.guest >> cpu_usage.guest_nice;
            proc_stat.cpu_usage.emplace_back(cpu_usage);
        } else if (line.starts_with("ctxt")) {
            auto iss = std::istringstream{line.substr(line.find_first_of(' '))};
            iss >> proc_stat.ctxt;
        } else if (line.starts_with("btime")) {
            auto iss = std::istringstream{line.substr(line.find_first_of(' '))};
            iss >> proc_stat.btime;
        } else if (line.starts_with("processes")) {
            auto iss = std::istringstream{line.substr(line.find_first_of(' '))};
            iss >> proc_stat.processes;
        } else if (line.starts_with("procs_running")) {
            auto iss = std::istringstream{line.substr(line.find_first_of(' '))};
            iss >> proc_stat.procs_running;
        } else if (line.starts_with("procs_blocked")) {
            auto iss = std::istringstream{line.substr(line.find_first_of(' '))};
            iss >> proc_stat.procs_blocked;
        }
    }
    return proc_stat;
}

auto monitor_proc_pid_stat(int pid) -> proc_pid_stat_t {
    auto content = read_file(std::format("/proc/{}/stat", pid));
    if (content.empty()) {
        return {};
    }

    auto proc_pid_stat = proc_pid_stat_t{};
    auto iss = std::istringstream{content};
    iss >> proc_pid_stat.pid >> proc_pid_stat.comm >> proc_pid_stat.state >> proc_pid_stat.ppid >>
        proc_pid_stat.pgrp >> proc_pid_stat.session >> proc_pid_stat.tty_nr >> proc_pid_stat.tpgid >>
        proc_pid_stat.flags >> proc_pid_stat.minflt >> proc_pid_stat.cminflt >> proc_pid_stat.majflt >>
        proc_pid_stat.cmajflt >> proc_pid_stat.utime >> proc_pid_stat.stime >> proc_pid_stat.cutime >>
        proc_pid_stat.cstime >> proc_pid_stat.priority >> proc_pid_stat.nice >> proc_pid_stat.num_threads >>
        proc_pid_stat.itrealvalue >> proc_pid_stat.starttime >> proc_pid_stat.vsize >> proc_pid_stat.rss >>
        proc_pid_stat.rsslim >> proc_pid_stat.startcode >> proc_pid_stat.endcode >> proc_pid_stat.startstack >>
        proc_pid_stat.kstkesp >> proc_pid_stat.kstkeip >> proc_pid_stat.signal >> proc_pid_stat.blocked;
    return proc_pid_stat;
}

auto monitor_fs_usage(const std::string &path) -> std::tuple<uint64_t, uint64_t> {
    struct statvfs stat;
    if (statvfs(path.data(), &stat) != 0) {
        return std::make_tuple(0ull, 0ull);
    }
    return std::make_tuple(stat.f_bavail * stat.f_bsize, stat.f_blocks * stat.f_bsize);
}