#pragma once
#include "monitor/monitor_inter.h"
#include <string>

namespace monitor {

/// Reads /proc/meminfo.
struct MemMonitor : MonitorInterface {
    bool Init() override;
    void Collect() override;

    double total_gb     = 0;
    double used_gb      = 0;
    double free_gb      = 0;
    double available_gb = 0;
    double cached_gb    = 0;
    double buffers_gb   = 0;
    double swap_total_gb= 0;
    double swap_used_gb = 0;
    double used_pct     = 0;
};

} // namespace monitor
