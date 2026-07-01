#pragma once
#include "monitor/monitor_inter.h"
#include "diagnose/cmd_runner.h"
#include <vector>
#include <string>
#include <cstdint>

namespace monitor {

struct CppDiagnoseEvent {
    std::string type;          // "offcpu" | "profile"
    std::string process_name;
    std::string thread_name;
    std::string file_path;
    int64_t     timestamp = 0;
    double      cpu_percent = 0;
};

/// Launches external eBPF offcpu / profile binaries as subprocesses.
/// Reads diagnose_conf.json for per-process filter rules.
struct BpfDiagnoser : MonitorInterface {
    bool Init() override;
    void Collect() override;
    void Reset() override;

    std::vector<CppDiagnoseEvent> events;

    // Config: set by MonitorPusher before Init()
    std::string output_dir_;
    std::string offcpu_bin_;
    std::string profile_bin_;

    void start_offcpu(const std::string& process, const std::string& thread,
                      int min_block_us, int max_block_us, int interval_s);
    void start_profile(const std::string& process, const std::string& thread,
                       int max_nr, int max_size_kb, const std::string& cpus);

    // File rotation: keep at most this many files per type in output_dir_.
    int limit_file_num_      = 3;
    int limit_file_size_mb_  = 50;  // offcpu output cap (passed to wrapper via -S)

private:
    struct OffcpuConfig {
        std::string process, thread;
        int min_block_us, max_block_us, interval_s;
    };
    struct ProfileConfig {
        std::string process, thread;
        int max_nr, max_size_kb;
        std::string cpus;
    };
    // Pairs a running subprocess with the event to emit on completion.
    struct RunnerEntry {
        CmdRunner        runner;
        CppDiagnoseEvent pending_ev;
    };

    void launch_offcpu(const OffcpuConfig& cfg);
    void launch_profile(const ProfileConfig& cfg);
    void cleanup_files(const std::string& prefix);
    void stop_all();

    std::vector<RunnerEntry>    runners_;
    std::vector<OffcpuConfig>   offcpu_configs_;
    std::vector<ProfileConfig>  profile_configs_;
};

} // namespace monitor
