#pragma once
#include "monitor/monitor_inter.h"
#include <string>

namespace monitor {

/// Reads /proc/loadavg to get 1/5/15 min load averages.
struct CpuLoadMonitor : MonitorInterface {
    bool Init() override;
    void Collect() override;

    double load_1m  = 0;
    double load_5m  = 0;
    double load_15m = 0;
    int    nr_cores = 1;
};

} // namespace monitor
