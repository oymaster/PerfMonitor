#include "monitor/mem_monitor.h"
#include "utils/read_file.h"
#include <sstream>
#include <unordered_map>

namespace monitor {

bool MemMonitor::Init() { return true; }

void MemMonitor::Collect() {
    auto content = ReadFile("/proc/meminfo");
    std::istringstream iss(content);
    std::unordered_map<std::string, uint64_t> m;
    std::string line;
    while (std::getline(iss, line)) {
        auto pos = line.find(':');
        if (pos == std::string::npos) continue;
        std::string key = line.substr(0, pos);
        std::string val = line.substr(pos + 1);
        // strip " kB" suffix
        auto k_pos = val.find("kB");
        if (k_pos == std::string::npos) continue;
        uint64_t kb = std::stoull(val.substr(0, k_pos));
        m[key] = kb;
    }

    auto to_gb = [](uint64_t kb) { return kb / (1024.0 * 1024.0); };

    total_gb     = to_gb(m["MemTotal"]);
    free_gb      = to_gb(m["MemFree"]);
    available_gb = to_gb(m["MemAvailable"]);
    cached_gb    = to_gb(m["Cached"]);
    buffers_gb   = to_gb(m["Buffers"]);
    swap_total_gb= to_gb(m["SwapTotal"]);
    swap_used_gb = to_gb(m["SwapTotal"] - m["SwapFree"]);
    used_gb      = total_gb - free_gb - cached_gb - buffers_gb;
    used_pct     = (total_gb > 0) ? (used_gb / total_gb * 100.0) : 0.0;
}

} // namespace monitor
