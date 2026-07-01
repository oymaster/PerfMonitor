#pragma once
#include "monitor/monitor_inter.h"
#include <vector>
#include <string>
#include <cstdint>
#include <memory>

namespace monitor {

// Lightweight C++ event structs — named with Cpp prefix to avoid collision
// with proto-generated classes of the same name in this namespace.
struct CppProcessEvent {
    std::string comm;
    uint32_t pid = 0;
    uint32_t ppid = 0;
    std::string filename;
    std::string event;  // "exec" | "exit"
    int64_t ts_ns = 0;
};

struct CppFlowEvent {
    uint32_t pid = 0;
    std::string comm, ppid_comm, exe;
    std::string src_ip, dst_ip;
    uint32_t src_port = 0, dst_port = 0;
    uint64_t bytes_sent = 0, bytes_recv = 0;
    uint64_t connect_latency_ns = 0;
    std::string state;
    uint64_t start_ns = 0, last_ns = 0;
};

/// eBPF-based process lifecycle + TCP flow observer.
/// Uses libbpf skeleton to load process_trace.bpf.o and tcp_flow.bpf.o
/// via open_and_load / attach / perf_buffer__poll.
///
/// On platforms without libbpf or when ENABLE_EBPF is not defined,
/// Init() returns true but Collect() is a no-op — graceful degradation.
struct ObserveCollector : MonitorInterface {
    ObserveCollector();
    ~ObserveCollector();

    bool Init() override;
    void Collect() override;
    void Reset() override;

    std::vector<CppProcessEvent> process_events;
    std::vector<CppFlowEvent>    flow_events;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    // Perf-buffer callbacks (called from BPF ring via libbpf).
    static void on_process_event(void* ctx, int cpu, void* data, uint32_t sz);
    static void on_flow_event(void* ctx, int cpu, void* data, uint32_t sz);
};

} // namespace monitor
