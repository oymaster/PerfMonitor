#include "host_manager.h"
#include <algorithm>
#include <cmath>

namespace monitor {

void HostManager::Update(const std::string& hostname, const HostSnapshot& snap) {
    std::lock_guard lock(mutex_);
    auto& entry = hosts_[hostname];

    // Accumulate diagnose events (keep last 20) before overwriting.
    auto prev_diag = std::move(entry.diagnose_events);
    entry = snap;
    entry.hostname = hostname;
    entry.health_score = ComputeHealth(snap);

    // Merge: append old events first, then new ones, keep tail 20.
    prev_diag.insert(prev_diag.end(),
                     snap.diagnose_events.begin(), snap.diagnose_events.end());
    if (prev_diag.size() > 20)
        prev_diag.erase(prev_diag.begin(), prev_diag.begin() + (prev_diag.size() - 20));
    entry.diagnose_events = std::move(prev_diag);
}

HostSnapshot HostManager::Get(const std::string& hostname) const {
    std::lock_guard lock(mutex_);
    auto it = hosts_.find(hostname);
    if (it != hosts_.end()) return it->second;
    return {};
}

std::vector<HostSnapshot> HostManager::GetAll() const {
    std::lock_guard lock(mutex_);
    std::vector<HostSnapshot> result;
    result.reserve(hosts_.size());
    for (const auto& kv : hosts_) {
        result.push_back(kv.second);
    }
    return result;
}

void HostManager::RemoveExpired(int64_t max_age_s) {
    std::lock_guard lock(mutex_);
    int64_t now = time(nullptr);
    auto it = hosts_.begin();
    while (it != hosts_.end()) {
        if (now - it->second.last_update > max_age_s) {
            it = hosts_.erase(it);
        } else {
            ++it;
        }
    }
}

double HostManager::ComputeHealth(const HostSnapshot& snap) {
    double score = 100.0;
    if (snap.load_1m > 8.0)  score -= (snap.load_1m - 8.0) * 5;
    if (snap.cpu_used_pct > 90) score -= (snap.cpu_used_pct - 90) * 2;
    if (snap.mem_used_pct > 85) score -= (snap.mem_used_pct - 85) * 2;
    if (snap.net_rx_dropped > 100) score -= 10;
    return std::max(0.0, score);
}

} // namespace monitor
