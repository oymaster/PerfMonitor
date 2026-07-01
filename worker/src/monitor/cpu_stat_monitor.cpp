#include "monitor/cpu_stat_monitor.h"
#include "utils/read_file.h"
#include <sstream>
#include <cmath>

namespace monitor {

bool CpuStatMonitor::Init() {
    // read initial snapshot
    std::string content = ReadFile("/proc/stat");
    std::istringstream iss(content);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.rfind("cpu ", 0) != 0) continue;
        std::istringstream ls(line.substr(4));
        ls >> prev_.user >> prev_.nice >> prev_.system
           >> prev_.idle >> prev_.iowait >> prev_.irq
           >> prev_.softirq >> prev_.steal;
        break;
    }
    return true;
}

void CpuStatMonitor::Collect() {
    CpuStatRaw cur;
    std::string content = ReadFile("/proc/stat");
    std::istringstream iss(content);
    std::string line;
    while (std::getline(iss, line)) {
        if (line.rfind("cpu ", 0) != 0) continue;
        std::istringstream ls(line.substr(4));
        ls >> cur.user >> cur.nice >> cur.system
           >> cur.idle >> cur.iowait >> cur.irq
           >> cur.softirq >> cur.steal;
        break;
    }

    uint64_t total_delta = cur.total() - prev_.total();
    if (total_delta == 0) return;

    auto calc = [&](uint64_t val, uint64_t prev) -> double {
        int64_t d = static_cast<int64_t>(val) - static_cast<int64_t>(prev);
        return (d < 0) ? 0.0 : 100.0 * d / total_delta;
    };

    pct.user    = calc(cur.user,    prev_.user);
    pct.system  = calc(cur.system,  prev_.system);
    pct.idle    = calc(cur.idle,    prev_.idle);
    pct.iowait  = calc(cur.iowait,  prev_.iowait);
    pct.steal   = calc(cur.steal,   prev_.steal);
    pct.softirq = calc(cur.softirq, prev_.softirq);

    prev_ = cur;
}

} // namespace monitor
