#include "monitor/net_monitor.h"
#include "utils/read_file.h"
#include <sstream>
#include <unordered_map>

namespace monitor {

struct NetRaw {
    uint64_t rx_bytes   = 0;
    uint64_t rx_packets = 0;
    uint64_t rx_errors  = 0;
    uint64_t rx_dropped = 0;
    uint64_t tx_bytes   = 0;
    uint64_t tx_packets = 0;
    uint64_t tx_errors  = 0;
    uint64_t tx_dropped = 0;
};

static std::unordered_map<std::string, NetRaw> prev_net_map;

bool NetMonitor::Init() {
    auto content = ReadFile("/proc/net/dev");
    std::istringstream iss(content);
    std::string line;
    // skip 2 header lines
    std::getline(iss, line);
    std::getline(iss, line);
    while (std::getline(iss, line)) {
        auto pos = line.find(':');
        if (pos == std::string::npos) continue;
        std::string iface = line.substr(0, pos);
        // trim left spaces
        auto start = iface.find_first_not_of(" \t");
        if (start != std::string::npos) iface = iface.substr(start);

        std::istringstream ls(line.substr(pos + 1));
        NetRaw r;
        ls >> r.rx_bytes >> r.rx_packets >> r.rx_errors >> r.rx_dropped
           >> std::ws; // skip fifo/frame/compressed/multicast
        uint64_t dummy;
        for (int i = 0; i < 4; ++i) ls >> dummy;
        ls >> r.tx_bytes >> r.tx_packets >> r.tx_errors >> r.tx_dropped;
        prev_net_map[iface] = r;
    }
    return true;
}

void NetMonitor::Collect() {
    auto content = ReadFile("/proc/net/dev");
    std::istringstream iss(content);
    std::string line;
    std::getline(iss, line);
    std::getline(iss, line);

    std::unordered_map<std::string, NetRaw> cur_map;
    while (std::getline(iss, line)) {
        auto pos = line.find(':');
        if (pos == std::string::npos) continue;
        std::string iface = line.substr(0, pos);
        auto start = iface.find_first_not_of(" \t");
        if (start != std::string::npos) iface = iface.substr(start);

        std::istringstream ls(line.substr(pos + 1));
        NetRaw r;
        ls >> r.rx_bytes >> r.rx_packets >> r.rx_errors >> r.rx_dropped;
        uint64_t dummy;
        for (int i = 0; i < 4; ++i) ls >> dummy;
        ls >> r.tx_bytes >> r.tx_packets >> r.tx_errors >> r.tx_dropped;
        cur_map[iface] = r;
    }

    interfaces.clear();
    for (auto& [iface, cur] : cur_map) {
        if (iface == "lo") continue;
        auto it = prev_net_map.find(iface);
        if (it == prev_net_map.end()) continue;

        NetRaw& prev = it->second;
        NetStats ns;
        ns.iface      = iface;
        ns.rx_mbps    = (cur.rx_bytes - prev.rx_bytes) * 8.0 / (1024 * 1024);
        ns.tx_mbps    = (cur.tx_bytes - prev.tx_bytes) * 8.0 / (1024 * 1024);
        ns.rx_packets = cur.rx_packets - prev.rx_packets;
        ns.tx_packets = cur.tx_packets - prev.tx_packets;
        ns.rx_dropped = cur.rx_dropped - prev.rx_dropped;
        ns.tx_dropped = cur.tx_dropped - prev.tx_dropped;
        ns.rx_errors  = cur.rx_errors  - prev.rx_errors;
        ns.tx_errors  = cur.tx_errors  - prev.tx_errors;
        interfaces.push_back(ns);
    }
    prev_net_map = std::move(cur_map);
}

} // namespace monitor
