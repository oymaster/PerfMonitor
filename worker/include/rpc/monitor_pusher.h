#pragma once
#include <memory>
#include <string>
#include <atomic>
#include <thread>
#include "monitor/monitor_inter.h"
#include "monitor/metric_collector.h"
#include "observe/observe_collector.h"
#include "diagnose/bpf_diagnoser.h"
#include "diagnose/perf_diagnoser.h"

namespace monitor {

/// Main Worker entry: collects all layers and pushes via gRPC to Manager.
struct MonitorPusher {
    MonitorPusher(const std::string& manager_addr, int interval_s = 10);
    ~MonitorPusher();

    void Run();
    void Stop();

private:
    void push_loop();
    void do_push();

    std::string manager_addr_;
    int         interval_s_;

    MetricCollector   metrics_;
    ObserveCollector  observer_;
    BpfDiagnoser      bpf_diagnoser_;
    PerfDiagnoser     perf_diagnoser_;

    std::atomic<bool> running_{false};
    std::thread        loop_thread_;

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace monitor
