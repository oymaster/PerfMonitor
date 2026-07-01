#include "observe/observe_collector.h"
#include <iostream>
#include <cstring>
#include <arpa/inet.h>

// BPF shared structs — must match kernel-side observe_events.h
#include "observe_events.h"

#ifdef ENABLE_EBPF
#include <bpf/libbpf.h>
#include "process_trace.skel.h"
#include "tcp_flow.skel.h"
#endif

namespace monitor {

// ============================================================
// Helpers: BPF event → C++ event conversion
// ============================================================
static inline const char* bpf_state_str(uint8_t state) {
    switch (state) {
        case 1:  return "ESTABLISHED";
        case 2:  return "SYN_SENT";
        case 3:  return "SYN_RECV";
        case 4:  return "FIN_WAIT1";
        case 5:  return "FIN_WAIT2";
        case 6:  return "TIME_WAIT";
        case 7:  return "CLOSE";
        case 8:  return "CLOSE_WAIT";
        case 9:  return "LAST_ACK";
        case 10: return "LISTEN";
        case 11: return "CLOSING";
        default: return "UNKNOWN";
    }
}

static CppProcessEvent bpf_to_process(const struct process_event* raw) {
    CppProcessEvent ev;
    ev.pid      = raw->pid;
    ev.ppid     = raw->ppid;
    ev.comm     = std::string(raw->comm, strnlen(raw->comm, MAX_COMM_LEN));
    ev.filename = std::string(raw->filename, strnlen(raw->filename, MAX_FILENAME_LEN));
    ev.event    = (raw->event_type == 0) ? "exec" : "exit";
    ev.ts_ns    = raw->ts_ns;
    return ev;
}

static CppFlowEvent bpf_to_flow(const struct flow_event* raw) {
    CppFlowEvent ev;
    ev.pid       = raw->pid;
    ev.comm      = std::string(raw->comm, strnlen(raw->comm, MAX_COMM_LEN));
    ev.ppid_comm = std::string(raw->ppid_comm, strnlen(raw->ppid_comm, MAX_COMM_LEN));
    ev.exe       = std::string(raw->exe, strnlen(raw->exe, MAX_FILENAME_LEN));
    ev.src_port  = raw->sport;
    ev.dst_port  = raw->dport;

    // Convert __u32[4] IPv4 to dotted string
    char ip_buf[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &raw->saddr[0], ip_buf, sizeof(ip_buf));
    ev.src_ip = ip_buf;
    inet_ntop(AF_INET, &raw->daddr[0], ip_buf, sizeof(ip_buf));
    ev.dst_ip = ip_buf;

    ev.bytes_sent          = raw->bytes_sent;
    ev.bytes_recv          = raw->bytes_recv;
    ev.connect_latency_ns  = raw->connect_latency_ns;
    ev.state               = bpf_state_str(raw->state);
    ev.start_ns            = raw->start_ns;
    ev.last_ns             = raw->last_ns;
    return ev;
}

// ============================================================
// Impl — holds libbpf skeletons and perf buffers
// ============================================================
struct ObserveCollector::Impl {
#ifdef ENABLE_EBPF
    struct process_trace_bpf* proc_skel = nullptr;
    struct tcp_flow_bpf*      flow_skel = nullptr;
    struct perf_buffer*       proc_pb   = nullptr;
    struct perf_buffer*       flow_pb   = nullptr;
#endif

    ~Impl() {
#ifdef ENABLE_EBPF
        if (proc_pb)  perf_buffer__free(proc_pb);
        if (flow_pb)  perf_buffer__free(flow_pb);
        if (proc_skel) {
            process_trace_bpf__destroy(proc_skel);
        }
        if (flow_skel) {
            tcp_flow_bpf__destroy(flow_skel);
        }
#endif
    }
};

// ============================================================
// Perf-buffer callbacks
// ============================================================
void ObserveCollector::on_process_event(void* ctx, int /*cpu*/, void* data, uint32_t sz) {
    if (sz < sizeof(struct process_event)) return;
    auto* self = static_cast<ObserveCollector*>(ctx);
    self->process_events.push_back(
        bpf_to_process(static_cast<const struct process_event*>(data)));
}

void ObserveCollector::on_flow_event(void* ctx, int /*cpu*/, void* data, uint32_t sz) {
    if (sz < sizeof(struct flow_event)) return;
    auto* self = static_cast<ObserveCollector*>(ctx);
    self->flow_events.push_back(
        bpf_to_flow(static_cast<const struct flow_event*>(data)));
}

// ============================================================
// ObserveCollector
// ============================================================
ObserveCollector::ObserveCollector() : impl_(std::make_unique<Impl>()) {}
ObserveCollector::~ObserveCollector() = default;

bool ObserveCollector::Init() {
#ifdef ENABLE_EBPF
    // --- 1. Load & attach process_trace ---
    impl_->proc_skel = process_trace_bpf__open();
    if (!impl_->proc_skel) {
        std::cerr << "[ObserveCollector] process_trace_bpf__open failed" << std::endl;
        return false;
    }
    if (process_trace_bpf__load(impl_->proc_skel)) {
        std::cerr << "[ObserveCollector] process_trace_bpf__load failed" << std::endl;
        return false;
    }
    if (process_trace_bpf__attach(impl_->proc_skel)) {
        std::cerr << "[ObserveCollector] process_trace_bpf__attach failed" << std::endl;
        return false;
    }

    // --- 2. Load & attach tcp_flow ---
    impl_->flow_skel = tcp_flow_bpf__open();
    if (!impl_->flow_skel) {
        std::cerr << "[ObserveCollector] tcp_flow_bpf__open failed" << std::endl;
        return false;
    }
    if (tcp_flow_bpf__load(impl_->flow_skel)) {
        std::cerr << "[ObserveCollector] tcp_flow_bpf__load failed" << std::endl;
        return false;
    }
    if (tcp_flow_bpf__attach(impl_->flow_skel)) {
        std::cerr << "[ObserveCollector] tcp_flow_bpf__attach failed" << std::endl;
        return false;
    }

    // --- 3. Set up perf buffers (Ubuntu libbpf 0.5: callbacks in opts struct) ---
    {
        struct perf_buffer_opts pb_opts = {};
        pb_opts.sample_cb = on_process_event;
        pb_opts.ctx       = this;
        impl_->proc_pb = perf_buffer__new(
            bpf_map__fd(impl_->proc_skel->maps.process_events),
            64, &pb_opts);
    }
    if (!impl_->proc_pb) {
        std::cerr << "[ObserveCollector] process perf_buffer__new failed" << std::endl;
        return false;
    }

    {
        struct perf_buffer_opts pb_opts = {};
        pb_opts.sample_cb = on_flow_event;
        pb_opts.ctx       = this;
        impl_->flow_pb = perf_buffer__new(
            bpf_map__fd(impl_->flow_skel->maps.flow_events),
            64, &pb_opts);
    }
    if (!impl_->flow_pb) {
        std::cerr << "[ObserveCollector] flow perf_buffer__new failed" << std::endl;
        return false;
    }

    std::cout << "[ObserveCollector] eBPF programs loaded and attached" << std::endl;
#endif // ENABLE_EBPF
    return true;
}

void ObserveCollector::Collect() {
#ifdef ENABLE_EBPF
    // Non-blocking poll: timeout 0 — drain any queued events without sleeping.
    if (impl_->proc_pb) {
        int ret = perf_buffer__poll(impl_->proc_pb, 0);
        if (ret < 0 && ret != -EINTR) {
            std::cerr << "[ObserveCollector] proc perf_buffer__poll error: " << ret << std::endl;
        }
    }
    if (impl_->flow_pb) {
        int ret = perf_buffer__poll(impl_->flow_pb, 0);
        if (ret < 0 && ret != -EINTR) {
            std::cerr << "[ObserveCollector] flow perf_buffer__poll error: " << ret << std::endl;
        }
    }
#endif
}

void ObserveCollector::Reset() {
    process_events.clear();
    flow_events.clear();
}

} // namespace monitor
