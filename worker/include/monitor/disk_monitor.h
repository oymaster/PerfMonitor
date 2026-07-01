#pragma once
#include "monitor/monitor_inter.h"
#include <string>
#include <vector>

namespace monitor {

struct DiskStats {
    std::string device;
    double      read_mbps   = 0;
    double      write_mbps  = 0;
    double      iops        = 0;
    double      io_latency_ms = 0;
};

/// Reads /proc/diskstats.
struct DiskMonitor : MonitorInterface {
    bool Init() override;
    void Collect() override;

    std::vector<DiskStats> disks;
};

} // namespace monitor
