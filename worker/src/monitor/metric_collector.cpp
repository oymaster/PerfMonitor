#include "monitor/metric_collector.h"

namespace monitor {

bool MetricCollector::Init() {
    host.Init();
    cpu_load.Init();
    cpu_stat.Init();
    mem.Init();
    disk.Init();
    net.Init();
    return true;
}

void MetricCollector::Collect() {
    cpu_load.Collect();
    cpu_stat.Collect();
    mem.Collect();
    disk.Collect();
    net.Collect();
}

} // namespace monitor
