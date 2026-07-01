#pragma once
#include "host_manager.h"
#include <string>
#include <thread>
#include <atomic>

namespace monitor {

/// Exposes Prometheus /metrics and /healthz over HTTP.
/// Uses embedded httplib (header-only).
class MetricsExporter {
public:
    explicit MetricsExporter(HostManager& host_mgr, int port = 8080);
    ~MetricsExporter();

    void Start();
    void Stop();

private:
    void serve_loop();

    HostManager& host_mgr_;
    int port_;
    std::thread thread_;
    std::atomic<bool> running_{false};
};

} // namespace monitor
