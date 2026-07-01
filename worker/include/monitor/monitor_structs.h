#pragma once
#include <cstdint>
#include <string>

namespace monitor {

/// /proc/stat raw snapshot (one per-cpu line aggregated into a single struct).
struct CpuStatRaw {
    uint64_t user    = 0;
    uint64_t nice    = 0;
    uint64_t system  = 0;
    uint64_t idle    = 0;
    uint64_t iowait  = 0;
    uint64_t irq     = 0;
    uint64_t softirq = 0;
    uint64_t steal   = 0;

    uint64_t total() const {
        return user + nice + system + idle + iowait + irq + softirq + steal;
    }
};

struct CpuStatPct {
    double user    = 0;
    double system  = 0;
    double idle    = 0;
    double iowait  = 0;
    double steal   = 0;
    double softirq = 0;
};

} // namespace monitor
