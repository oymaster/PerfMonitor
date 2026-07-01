#pragma once
#include "monitor/monitor_inter.h"
#include <string>
#include <vector>

namespace monitor {

struct NetStats {
    std::string iface;
    double      rx_mbps    = 0;
    double      tx_mbps    = 0;
    uint64_t    rx_packets = 0;
    uint64_t    tx_packets = 0;
    uint64_t    rx_dropped = 0;
    uint64_t    tx_dropped = 0;
    uint64_t    rx_errors  = 0;
    uint64_t    tx_errors  = 0;
};

/// Reads /proc/net/dev.
struct NetMonitor : MonitorInterface {
    bool Init() override;
    void Collect() override;

    std::vector<NetStats> interfaces;
};

} // namespace monitor
