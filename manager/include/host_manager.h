#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <mutex>
#include <cstdint>

namespace monitor {

struct DiagnoseRecord {
    std::string type;          // "offcpu" | "profile" | "perf"
    std::string process_name;
    std::string file_path;
    int64_t     timestamp   = 0;
    double      cpu_percent = 0;
};

struct HostSnapshot {
    double load_1m = 0;
    double cpu_used_pct = 0;
    double mem_used_pct = 0;
    double disk_iops = 0;
    double net_rx_mbps = 0;
    double net_tx_mbps = 0;
    double net_rx_dropped = 0;
    int64_t last_update = 0;
    double health_score = 100.0;
    std::string hostname;

    // Observe counters (accumulated since last snapshot)
    int64_t proc_exec_count = 0;
    int64_t proc_exit_count = 0;
    int64_t tcp_flow_count  = 0;
    int64_t tcp_bytes_sent  = 0;
    int64_t tcp_bytes_recv  = 0;

    // Diagnose events (last 20 kept across pushes)
    std::vector<DiagnoseRecord> diagnose_events;
};

/// Manages per-host metric snapshots and computes health scores.
class HostManager {
public:
    void Update(const std::string& hostname, const HostSnapshot& snap);
    HostSnapshot Get(const std::string& hostname) const;

    /// Returns all current host snapshots (for Prometheus exporter).
    std::vector<HostSnapshot> GetAll() const;

    void RemoveExpired(int64_t max_age_s = 60);

    // Simple health score: 100 - penalty for each metric exceeding threshold
    static double ComputeHealth(const HostSnapshot& snap);

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, HostSnapshot> hosts_;
};

} // namespace monitor
