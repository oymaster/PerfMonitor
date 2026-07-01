#include "monitor/disk_monitor.h"
#include "utils/read_file.h"
#include <sstream>
#include <unordered_map>
#include <cmath>

namespace monitor {

struct DiskRaw {
    uint64_t reads_done   = 0;
    uint64_t writes_done  = 0;
    uint64_t read_sectors = 0;
    uint64_t write_sectors= 0;
    uint64_t io_ticks     = 0;
};

static std::unordered_map<std::string, DiskRaw> prev_map;

bool DiskMonitor::Init() {
    // read once to seed prev_map
    auto content = ReadFile("/proc/diskstats");
    std::istringstream iss(content);
    std::string line;
    while (std::getline(iss, line)) {
        std::istringstream ls(line);
        int major, minor;
        std::string dev;
        DiskRaw r;
        ls >> major >> minor >> dev
           >> r.reads_done >> std::ws; // skip merged
        uint64_t dummy;
        ls >> dummy >> dummy;          // read_merged, read_sectors_old
        ls >> r.read_sectors >> dummy; // read_time_ms
        ls >> r.writes_done >> dummy >> dummy;
        ls >> r.write_sectors >> dummy;
        ls >> dummy >> r.io_ticks;
        if (!dev.empty() && dev.find("loop") != 0 && dev.find("ram") != 0) {
            prev_map[dev] = r;
        }
    }
    return true;
}

void DiskMonitor::Collect() {
    auto content = ReadFile("/proc/diskstats");
    std::istringstream iss(content);
    std::string line;
    std::unordered_map<std::string, DiskRaw> cur_map;

    while (std::getline(iss, line)) {
        std::istringstream ls(line);
        int major, minor;
        std::string dev;
        DiskRaw r;
        ls >> major >> minor >> dev
           >> r.reads_done;
        uint64_t dummy;
        ls >> dummy >> dummy;
        ls >> r.read_sectors >> dummy;
        ls >> r.writes_done >> dummy >> dummy;
        ls >> r.write_sectors >> dummy;
        ls >> dummy >> r.io_ticks;

        if (dev.empty() || dev.find("loop") == 0 || dev.find("ram") == 0) continue;
        cur_map[dev] = r;
    }

    disks.clear();
    for (auto& [dev, cur] : cur_map) {
        auto it = prev_map.find(dev);
        if (it == prev_map.end()) continue;

        DiskRaw& prev = it->second;
        double read_mb  = (cur.read_sectors  - prev.read_sectors)  * 512.0 / (1024 * 1024);
        double write_mb = (cur.write_sectors - prev.write_sectors) * 512.0 / (1024 * 1024);
        double iops     = (cur.reads_done + cur.writes_done) - (prev.reads_done + prev.writes_done);
        double io_ms    = (cur.io_ticks - prev.io_ticks) * 1.0; // rough

        DiskStats ds;
        ds.device       = dev;
        ds.read_mbps    = read_mb;
        ds.write_mbps   = write_mb;
        ds.iops         = iops;
        ds.io_latency_ms= io_ms;
        disks.push_back(ds);
    }
    prev_map = std::move(cur_map);
}

} // namespace monitor
