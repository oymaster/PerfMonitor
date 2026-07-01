#include "diagnose/perf_diagnoser.h"
#include "config/config_manager.h"
#include <sys/stat.h>
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <algorithm>
#include <iostream>
#include <chrono>
#include <fstream>
#include <sstream>

namespace monitor {

// ---------------------------------------------------------------
//  /proc scan helpers (ported from monitor_app logic)
// ---------------------------------------------------------------
static pid_t read_pid_by_name(const std::string& pname) {
    DIR* procfs = opendir("/proc");
    if (!procfs) return 0;

    struct dirent* ent;
    while ((ent = readdir(procfs)) != nullptr) {
        if (!ent->d_name || *ent->d_name < '0' || *ent->d_name > '9')
            continue;

        pid_t p = strtoul(ent->d_name, nullptr, 10);
        char path[512], buf[256];
        snprintf(path, sizeof(path), "/proc/%d/comm", p);

        int fd = open(path, O_RDONLY);
        if (fd < 0) continue;

        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n <= 0) continue;

        buf[n] = '\0';
        std::string comm(buf);
        if (!comm.empty() && comm.back() == '\n') comm.pop_back();

        if (comm == pname) {
            closedir(procfs);
            return p;
        }
    }
    closedir(procfs);
    return 0;
}

// Read CPU usage from /proc/[pid]/stat (utime + stime).
// Returns -1 on failure.
static double read_proc_cpu_pct(pid_t pid, uint64_t prev_utime, uint64_t prev_stime,
                                 uint64_t prev_total_time, uint64_t delta_ms) {
    char path[512];
    snprintf(path, sizeof(path), "/proc/%d/stat", pid);

    std::ifstream f(path);
    if (!f.is_open()) return -1.0;

    std::string line;
    std::getline(f, line);

    // /proc/[pid]/stat format: pid (comm) state ppid ... utime stime ...
    // Find the closing ')' after comm, then fields 14,15 are utime,stime
    auto close_paren = line.rfind(')');
    if (close_paren == std::string::npos) return -1.0;

    std::istringstream ss(line.substr(close_paren + 2));
    std::string field;
    uint64_t utime = 0, stime = 0;

    // state(3) ppid(4) pgrp(5) session(6) tty_nr(7) tpgid(8) flags(9)
    // minflt(10) cminflt(11) majflt(12) cmajflt(13) utime(14) stime(15)
    // Skip 11 fields (state through cmajflt) to reach utime at position 14.
    for (int i = 1; i <= 11 && ss >> field; ++i) {}
    if (ss >> utime >> stime) {
        uint64_t total = utime + stime;
        uint64_t delta = total - (prev_utime + prev_stime);
        if (prev_total_time == 0) return -1.0; // first sample

        double pct = (delta * 100.0) / (double)delta_ms;
        return pct;
    }
    return -1.0;
}

// ---------------------------------------------------------------
//  PerfDiagnoser
// ---------------------------------------------------------------
bool PerfDiagnoser::Init() {
    for (size_t pos = 1; pos <= output_dir_.size(); ++pos) {
        if (pos == output_dir_.size() || output_dir_[pos] == '/') {
            mkdir(output_dir_.substr(0, pos).c_str(), 0755);
        }
    }
    return true;
}

void PerfDiagnoser::Collect() {
    // Start background worker thread on first Collect call.
    if (!running_.exchange(true)) {
        worker_ = std::thread(&PerfDiagnoser::worker_loop, this);
    }
}

void PerfDiagnoser::Reset() {
    // Only clear events for next push; keep the monitoring thread running
    // so overflow_timers_ can accumulate across push intervals.
    events.clear();
}

void PerfDiagnoser::Stop() {
    running_ = false;
    if (worker_.joinable()) worker_.join();
    perf_runner_.Stop();
    events.clear();
    triggered_tags_.clear();
}

void PerfDiagnoser::worker_loop() {
    // Previous sample cache for CPU delta calculation
    struct ProcSample { uint64_t utime, stime, total_time; };
    std::map<pid_t, ProcSample> prev_samples;
    uint64_t prev_wall = 0;

    while (running_) {
        uint64_t now_wall = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        uint64_t delta_ms = (prev_wall > 0) ? (now_wall - prev_wall) : 5000;
        prev_wall = now_wall;

        // When the previous perf record finishes: emit its event (if file has data)
        // and allow re-triggering.
        if (!triggered_tags_.empty() && !perf_runner_.IsRunning()) {
            if (pending_ev_.pending) {
                struct stat st{};
                if (stat(pending_ev_.file_path.c_str(), &st) == 0 && st.st_size > 0) {
                    CppDiagnoseEvent ev;
                    ev.type         = pending_ev_.type;
                    ev.process_name = pending_ev_.process_name;
                    ev.file_path    = pending_ev_.file_path;
                    ev.timestamp    = pending_ev_.timestamp;
                    ev.cpu_percent  = pending_ev_.cpu_percent;
                    events.push_back(ev);
                }
                pending_ev_.pending = false;
                cleanup_files("perf_");
            }
            triggered_tags_.clear();
        }

        // Scan each target process
        for (const auto& proc_name : target_processes_) {
            std::string tag = proc_name + "_proc";
            if (triggered_tags_.count(tag)) continue;

            pid_t pid = read_pid_by_name(proc_name);
            if (pid == 0) continue;

            auto& prev = prev_samples[pid];
            double cpu_pct = read_proc_cpu_pct(pid, prev.utime, prev.stime,
                                                prev.total_time, delta_ms);

            // Refresh previous sample
            {
                char path[512];
                snprintf(path, sizeof(path), "/proc/%d/stat", pid);
                std::ifstream f(path);
                if (f.is_open()) {
                    std::string line;
                    std::getline(f, line);
                    auto p = line.rfind(')');
                    if (p != std::string::npos) {
                        std::istringstream ss(line.substr(p + 2));
                        std::string field;
                        for (int i = 1; i <= 11 && ss >> field; ++i) {}
                        ss >> prev.utime >> prev.stime;
                        prev.total_time = prev.utime + prev.stime;
                    }
                }
            }

            if (cpu_pct < 0) continue; // first sample, no delta yet

            // Scale jiffies→ms using HZ (typically 100 on Linux)
            long hz = sysconf(_SC_CLK_TCK);
            if (hz > 0) cpu_pct = cpu_pct * 1000.0 / hz;

            if (cpu_pct >= cpu_threshold_pct_) {
                overflow_timers_[tag] += 5; // 5s interval
            } else {
                overflow_timers_[tag] = 0;
            }

            if (overflow_timers_[tag] >= overflow_duration_s_) {
                std::string out_file = output_dir_ + "/perf_" + proc_name + "_" +
                                       std::to_string(time(nullptr)) + ".data";

                std::vector<std::string> args = {
                    "record", "-g",
                    "-p", std::to_string(pid),
                    "-o", out_file,
                    "--", "sleep", "5"
                };

                CmdRunner runner;
                // Pass empty output_file: perf writes binary data via -o flag;
                // redirecting stderr to the same file would corrupt the binary format.
                if (runner.Start(perf_bin_, args, "")) {
                    perf_runner_ = std::move(runner);
                    triggered_tags_.insert(tag);
                    pending_ev_.file_path    = out_file;
                    pending_ev_.process_name = proc_name;
                    pending_ev_.cpu_percent  = cpu_pct;
                    pending_ev_.timestamp    = time(nullptr);
                    pending_ev_.type         = "perf";
                    pending_ev_.pending      = true;
                }
                overflow_timers_[tag] = 0;
            }
        }

        std::this_thread::sleep_for(std::chrono::seconds(5));
    }
}

void PerfDiagnoser::cleanup_files(const std::string& prefix) {
    if (limit_file_num_ <= 0) return;
    DIR* d = opendir(output_dir_.c_str());
    if (!d) return;
    std::vector<std::pair<time_t, std::string>> files;
    struct dirent* ent;
    while ((ent = readdir(d)) != nullptr) {
        if (ent->d_name[0] == '.') continue;
        std::string name(ent->d_name);
        if (name.compare(0, prefix.size(), prefix) != 0) continue;
        std::string full = output_dir_ + "/" + name;
        struct stat st{};
        if (stat(full.c_str(), &st) == 0)
            files.emplace_back(st.st_mtime, full);
    }
    closedir(d);
    if ((int)files.size() <= limit_file_num_) return;
    std::sort(files.begin(), files.end());
    int to_delete = (int)files.size() - limit_file_num_;
    for (int i = 0; i < to_delete; ++i)
        unlink(files[i].second.c_str());
}

bool PerfDiagnoser::check_cpu_threshold() {
    // Legacy entry — kept for compatibility. Real logic is in worker_loop().
    return !events.empty();
}

} // namespace monitor
