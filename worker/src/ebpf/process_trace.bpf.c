// SPDX-License-Identifier: GPL-2.0
// eBPF program: trace process exec and exit events.
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include "observe_events.h"

// Kernel macros not re-exported by libbpf headers when vmlinux.h is used
#ifndef BPF_ANY
#define BPF_ANY         0
#endif
#ifndef BPF_NOEXIST
#define BPF_NOEXIST     1
#endif
#ifndef BPF_F_CURRENT_CPU
#define BPF_F_CURRENT_CPU 0xffffffffULL
#endif

struct {
    __uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
    __uint(key_size, sizeof(__u32));
    __uint(value_size, sizeof(__u32));
} process_events SEC(".maps");

// Cache: pid → process info for correlating with flow events.
struct proc_info {
    __u32 ppid;
    char  comm[MAX_COMM_LEN];
    char  filename[MAX_FILENAME_LEN];
};

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 65536);
    __type(key, __u32);      // pid
    __type(value, struct proc_info);
} processes SEC(".maps");

SEC("tracepoint/sched/sched_process_exec")
int trace_exec(struct trace_event_raw_sched_process_exec *ctx)
{
    __u32 pid = bpf_get_current_pid_tgid() >> 32;

    struct process_event ev = {};
    ev.pid = pid;
    ev.ts_ns = bpf_ktime_get_ns();
    ev.event_type = 0; // exec

    struct task_struct *task = (void *)bpf_get_current_task();
    __u32 ppid = BPF_CORE_READ(task, real_parent, tgid);
    ev.ppid = ppid;

    bpf_get_current_comm(&ev.comm, sizeof(ev.comm));

    // Read filename from ctx (__data_loc encodes offset in lower 16 bits)
    bpf_probe_read_str(&ev.filename, sizeof(ev.filename),
                       (void*)ctx + (ctx->__data_loc_filename & 0xffff));

    // Cache for flow events
    struct proc_info info = {};
    info.ppid = ppid;
    __builtin_memcpy(info.comm, ev.comm, sizeof(ev.comm));
    __builtin_memcpy(info.filename, ev.filename, sizeof(ev.filename));
    bpf_map_update_elem(&processes, &pid, &info, BPF_ANY);

    bpf_perf_event_output(ctx, &process_events, BPF_F_CURRENT_CPU,
                          &ev, sizeof(ev));
    return 0;
}

SEC("tracepoint/sched/sched_process_exit")
int trace_exit(struct trace_event_raw_sched_process_template *ctx)
{
    __u32 pid = bpf_get_current_pid_tgid() >> 32;

    struct process_event ev = {};
    ev.pid = pid;
    ev.ts_ns = bpf_ktime_get_ns();
    ev.event_type = 1; // exit
    bpf_get_current_comm(&ev.comm, sizeof(ev.comm));

    // Remove from cache
    bpf_map_delete_elem(&processes, &pid);

    bpf_perf_event_output(ctx, &process_events, BPF_F_CURRENT_CPU,
                          &ev, sizeof(ev));
    return 0;
}

char LICENSE[] SEC("license") = "GPL";
