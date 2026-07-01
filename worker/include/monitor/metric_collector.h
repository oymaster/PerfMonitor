#pragma once
#include "monitor/monitor_inter.h"
#include "monitor/cpu_load_monitor.h"
#include "monitor/cpu_stat_monitor.h"
#include "monitor/disk_monitor.h"
#include "monitor/mem_monitor.h"
#include "monitor/net_monitor.h"
#include "monitor/host_info_monitor.h"
#include <memory>
#include <vector>

namespace monitor {

/// Aggregates all first-layer metric monitors.
struct MetricCollector : MonitorInterface {
    bool Init() override;
    void Collect() override;

    HostInfoMonitor host;
    CpuLoadMonitor  cpu_load;
    CpuStatMonitor  cpu_stat;
    MemMonitor      mem;
    DiskMonitor     disk;
    NetMonitor      net;
};

} // namespace monitor
