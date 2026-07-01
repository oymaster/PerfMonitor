#include "monitor/cpu_load_monitor.h"
#include "utils/read_file.h"
#include <sstream>
#include <unistd.h>

namespace monitor {

bool CpuLoadMonitor::Init() {
    nr_cores = static_cast<int>(sysconf(_SC_NPROCESSORS_ONLN));
    if (nr_cores < 1) nr_cores = 1;
    return true;
}

void CpuLoadMonitor::Collect() {
    auto content = ReadFile("/proc/loadavg");
    if (content.empty()) return;

    std::istringstream iss(content);
    iss >> load_1m >> load_5m >> load_15m;
}

} // namespace monitor
