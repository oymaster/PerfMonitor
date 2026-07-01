#pragma once
#include "monitor/monitor_inter.h"
#include "monitor/monitor_structs.h"

namespace monitor {

/// Reads /proc/stat to compute CPU usage percentages (delta-based).
struct CpuStatMonitor : MonitorInterface {
    bool Init() override;
    void Collect() override;

    CpuStatPct pct;
private:
    CpuStatRaw prev_;
};

} // namespace monitor
