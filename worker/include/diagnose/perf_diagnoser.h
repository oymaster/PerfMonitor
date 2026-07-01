#pragma once
#include "monitor/monitor_inter.h"
#include "diagnose/bpf_diagnoser.h"
#include "diagnose/cmd_runner.h"
#include <vector>
#include <string>
#include <set>
#include <map>
#include <thread>
#include <atomic>

namespace monitor {

/// Condition-triggered perf record.
/// When a process/thread's CPU% exceeds threshold for N consecutive seconds,
/// launches `perf record -p <pid> -t <tid> -g -- sleep 5`.
/// Ported from monitor_app PerfMonitorConnector — reads /proc/[pid]/stat
/// for real CPU usage with delta-based calculation and debounce timer.
struct PerfDiagnoser : MonitorInterface {
    ~PerfDiagnoser() { Stop(); }
    bool Init() override;
    void Collect() override;
    void Reset() override;  // clear events only; thread keeps running
    void Stop();            // stop monitoring thread and release all resources

    std::vector<CppDiagnoseEvent> events;

    // Config: set by MonitorPusher before Init()
    std::string output_dir_;
    std::string perf_bin_;
    int    cpu_threshold_pct_ = 80;
    int    overflow_duration_s_ = 10;
    int    limit_file_num_ = 3;
    std::vector<std::string> target_processes_;
    std::vector<std::string> target_threads_;

private:
    void worker_loop();
    void cleanup_files(const std::string& prefix);
    bool check_cpu_threshold();

    CmdRunner perf_runner_;

    // Pending event — populated when perf starts, emitted when perf finishes.
    struct PendingPerfEvent {
        bool        pending     = false;
        std::string type, process_name, file_path;
        int64_t     timestamp   = 0;
        double      cpu_percent = 0;
    } pending_ev_;

    // state
    std::set<std::string>      triggered_tags_;
    std::map<std::string, int> overflow_timers_;
    std::thread                worker_;
    std::atomic<bool>          running_{false};
};

} // namespace monitor
