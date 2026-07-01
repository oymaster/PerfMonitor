#pragma once
#include "monitor/monitor_inter.h"
#include <string>

namespace monitor {

/// Collects hostname and primary IP.
struct HostInfoMonitor : MonitorInterface {
    bool Init() override;
    void Collect() override {}

    std::string hostname;
    std::string ip;
};

} // namespace monitor
