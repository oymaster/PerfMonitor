#include "diagnose/bpf_diagnoser.h"
#include "config/config_manager.h"
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <unistd.h>
#include <cstring>
#include <algorithm>
#include <iostream>

namespace monitor {

// ---------------------------------------------------------------
//  ReadProc — resolve process/thread name → pid/tid via /proc
//  (ported from monitor_app utils/read_proc)
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

static pid_t read_tid_by_name(const std::string& pname, const std::string& tname) {
    pid_t pid = read_pid_by_name(pname);
    if (pid == 0) return 0;

    std::string task_dir = "/proc/" + std::to_string(pid) + "/task";
    DIR* taskfs = opendir(task_dir.c_str());
    if (!taskfs) return 0;

    struct dirent* ent;
    while ((ent = readdir(taskfs)) != nullptr) {
        if (!ent->d_name || *ent->d_name < '0' || *ent->d_name > '9')
            continue;

        pid_t tid = strtoul(ent->d_name, nullptr, 10);
        char path[512], buf[256];
        snprintf(path, sizeof(path), "/proc/%d/task/%d/comm", pid, tid);

        int fd = open(path, O_RDONLY);
        if (fd < 0) continue;

        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        close(fd);
        if (n <= 0) continue;

        buf[n] = '\0';
        std::string comm(buf);
        if (!comm.empty() && comm.back() == '\n') comm.pop_back();

        if (comm == tname) {
            closedir(taskfs);
            return tid;
        }
    }
    closedir(taskfs);
    return 0;
}

// ---------------------------------------------------------------
//  BpfDiagnoser
// ---------------------------------------------------------------
bool BpfDiagnoser::Init() {
    // mkdir -p equivalent: create each path component
    for (size_t pos = 1; pos <= output_dir_.size(); ++pos) {
        if (pos == output_dir_.size() || output_dir_[pos] == '/') {
            mkdir(output_dir_.substr(0, pos).c_str(), 0755);
        }
    }
    return true;
}

void BpfDiagnoser::Collect() {
    // Reap finished runners; emit event only when file has data (tool completed).
    for (auto it = runners_.begin(); it != runners_.end(); ) {
        if (!it->runner.IsRunning()) {
            struct stat st{};
            if (stat(it->pending_ev.file_path.c_str(), &st) == 0 && st.st_size > 0) {
                events.push_back(it->pending_ev);
                cleanup_files(it->pending_ev.type);  // rotate old files
            }
            it = runners_.erase(it);
        } else {
            ++it;
        }
    }

    // Once all BCC tools have finished, restart them for the next cycle.
    if (runners_.empty() && (!offcpu_configs_.empty() || !profile_configs_.empty())) {
        for (auto& cfg : offcpu_configs_)  launch_offcpu(cfg);
        for (auto& cfg : profile_configs_) launch_profile(cfg);
    }
}

void BpfDiagnoser::Reset() {
    // Only clear events for next push; keep BCC tools running so they
    // complete their collection interval before Collect() reaps them.
    events.clear();
}

static std::string make_tag(const std::string& process, const std::string& thread) {
    return process + (thread.empty() ? "" : "_" + thread);
}

void BpfDiagnoser::start_offcpu(const std::string& process, const std::string& thread,
                                 int min_block_us, int max_block_us, int interval_s) {
    offcpu_configs_.push_back({process, thread, min_block_us, max_block_us, interval_s});
    launch_offcpu(offcpu_configs_.back());
}

void BpfDiagnoser::launch_offcpu(const OffcpuConfig& cfg) {
    std::vector<std::string> pid_args, tid_args;
    if (!cfg.process.empty()) {
        pid_t pid = read_pid_by_name(cfg.process);
        if (pid > 0) pid_args = {"-p", std::to_string(pid)};
    }
    if (!cfg.thread.empty() && !cfg.process.empty()) {
        pid_t tid = read_tid_by_name(cfg.process, cfg.thread);
        if (tid > 0) tid_args = {"-t", std::to_string(tid)};
    }

    std::string tag      = make_tag(cfg.process, cfg.thread);
    std::string out_file = output_dir_ + "/offcpu_" + tag + "_" +
                           std::to_string(time(nullptr)) + ".data";

    std::vector<std::string> args = {
        "-m", std::to_string(cfg.min_block_us),
        "-M", std::to_string(cfg.max_block_us),
        "-o", out_file,
        "-i", std::to_string(cfg.interval_s),
        "-S", std::to_string(limit_file_size_mb_),
    };
    args.insert(args.end(), pid_args.begin(), pid_args.end());
    args.insert(args.end(), tid_args.begin(), tid_args.end());

    CmdRunner runner;
    if (runner.Start(offcpu_bin_, args, out_file)) {
        RunnerEntry entry;
        entry.runner              = std::move(runner);
        entry.pending_ev.type         = "offcpu";
        entry.pending_ev.process_name = cfg.process;
        entry.pending_ev.thread_name  = cfg.thread;
        entry.pending_ev.file_path    = out_file;
        entry.pending_ev.timestamp    = time(nullptr);
        runners_.push_back(std::move(entry));
    }
}

void BpfDiagnoser::start_profile(const std::string& process, const std::string& thread,
                                  int max_nr, int max_size_kb, const std::string& cpus) {
    profile_configs_.push_back({process, thread, max_nr, max_size_kb, cpus});
    launch_profile(profile_configs_.back());
}

void BpfDiagnoser::launch_profile(const ProfileConfig& cfg) {
    std::vector<std::string> pid_args, tid_args;
    if (!cfg.process.empty()) {
        pid_t pid = read_pid_by_name(cfg.process);
        if (pid > 0) pid_args = {"-p", std::to_string(pid)};
    }
    if (!cfg.thread.empty() && !cfg.process.empty()) {
        pid_t tid = read_tid_by_name(cfg.process, cfg.thread);
        if (tid > 0) tid_args = {"-t", std::to_string(tid)};
    }

    std::string tag      = make_tag(cfg.process, cfg.thread);
    std::string out_file = output_dir_ + "/profile_" + tag + "_" +
                           std::to_string(time(nullptr)) + ".data";

    std::vector<std::string> args = {
        "-N", std::to_string(cfg.max_nr),
        "-M", std::to_string(cfg.max_size_kb),
        "-C", cfg.cpus,
        "-o", out_file,
    };
    args.insert(args.end(), pid_args.begin(), pid_args.end());
    args.insert(args.end(), tid_args.begin(), tid_args.end());

    CmdRunner runner;
    if (runner.Start(profile_bin_, args, out_file)) {
        RunnerEntry entry;
        entry.runner              = std::move(runner);
        entry.pending_ev.type         = "profile";
        entry.pending_ev.process_name = cfg.process;
        entry.pending_ev.thread_name  = cfg.thread;
        entry.pending_ev.file_path    = out_file;
        entry.pending_ev.timestamp    = time(nullptr);
        runners_.push_back(std::move(entry));
    }
}

void BpfDiagnoser::stop_all() {
    for (auto& e : runners_) e.runner.Stop();
    runners_.clear();
}

void BpfDiagnoser::cleanup_files(const std::string& type_prefix) {
    if (limit_file_num_ <= 0) return;

    DIR* d = opendir(output_dir_.c_str());
    if (!d) return;

    // Collect matching files with their mtimes.
    std::vector<std::pair<time_t, std::string>> files;
    struct dirent* ent;
    while ((ent = readdir(d)) != nullptr) {
        if (ent->d_name[0] == '.') continue;
        std::string name(ent->d_name);
        if (name.compare(0, type_prefix.size(), type_prefix) != 0) continue;
        std::string full = output_dir_ + "/" + name;
        struct stat st{};
        if (stat(full.c_str(), &st) == 0)
            files.emplace_back(st.st_mtime, full);
    }
    closedir(d);

    if ((int)files.size() <= limit_file_num_) return;

    // Sort oldest-first, delete the excess.
    std::sort(files.begin(), files.end());
    int to_delete = (int)files.size() - limit_file_num_;
    for (int i = 0; i < to_delete; ++i)
        unlink(files[i].second.c_str());
}

} // namespace monitor
