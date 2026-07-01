#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace monitor {

/// JSON-based configuration manager with hot-reload.
/// Default config: conf/diagnose_conf.json
struct ConfigManager {
    bool Load(const std::string& path);
    bool Reload(const std::string& path);

    // BpfDiagnoser settings
    struct OffcpuEntry {
        std::string process_name;
        std::string thread_name;
        int min_block_us  = 100;
        int max_block_us  = 1000000;
        int interval_s    = 60;
        int limit_file_size_mb = 50;
        int limit_file_num     = 3;
    };

    struct ProfileEntry {
        std::string process_name;
        std::string thread_name;
        int max_nr       = 1000;
        int max_size_kb  = 4096;
        std::string cpus = "0-3";
        int cpu_threshold_pct = 50;
    };

    std::vector<OffcpuEntry>  offcpu_entries;
    std::vector<ProfileEntry> profile_entries;

    // PerfDiagnoser settings
    int perf_cpu_threshold_pct    = 80;
    int perf_overflow_duration_s  = 10;
    std::vector<std::string> perf_target_processes;
    std::vector<std::string> perf_target_threads;

    // paths
    std::string offcpu_bin    = "/usr/bin/offcpu";
    std::string profile_bin   = "/usr/bin/profile";
    std::string perf_bin      = "/usr/bin/perf";
    std::string output_dir    = "/tmp/monitor_system/diagnose";

    nlohmann::json raw;
};

} // namespace monitor
