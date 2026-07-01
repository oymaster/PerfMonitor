// SPDX-License-Identifier: GPL-2.0
// eBPF program: trace TCP connection flows (IPv4 only).
#include "vmlinux.h"
#include <bpf/bpf_helpers.h>
#include <bpf/bpf_tracing.h>
#include <bpf/bpf_core_read.h>
#include <bpf/bpf_endian.h>
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
#ifndef AF_INET
#define AF_INET 2
#endif

// Key for flow tracking
struct flow_key {
    __u32 pid;
    __u32 saddr;
    __u32 daddr;
    __u16 sport;
    __u16 dport;
    __u16 family;
    __u8  protocol;
};

// Accumulated flow stats
struct flow_val {
    __u64 bytes_sent;
    __u64 bytes_recv;
    __u64 start_ns;
    __u64 last_ns;
    __u8  state;
};

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 65536);
    __type(key, struct flow_key);
    __type(value, struct flow_val);
} flow_map SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_PERF_EVENT_ARRAY);
    __uint(key_size, sizeof(__u32));
    __uint(value_size, sizeof(__u32));
} flow_events SEC(".maps");

// pid → connect timestamp for latency calc
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 65536);
    __type(key, __u32);
    __type(value, __u64);
} connect_start SEC(".maps");

// Note: proc_info cross-map lookup removed — two separate BPF skeletons
// cannot share maps without explicit pinning. ppid_comm/exe will be empty.

static __always_inline void fill_flow_key(struct flow_key *key, struct sock *sk) {
    key->pid = bpf_get_current_pid_tgid() >> 32;
    key->saddr = BPF_CORE_READ(sk, __sk_common.skc_rcv_saddr);
    key->daddr = BPF_CORE_READ(sk, __sk_common.skc_daddr);
    key->sport = BPF_CORE_READ(sk, __sk_common.skc_num);
    key->dport = bpf_ntohs(BPF_CORE_READ(sk, __sk_common.skc_dport));
    key->family = BPF_CORE_READ(sk, __sk_common.skc_family);
    key->protocol = IPPROTO_TCP;
}

static __always_inline void emit_flow(void *ctx, struct flow_key *key, struct flow_val *val) {
    struct flow_event ev = {};
    ev.pid = key->pid;
    bpf_get_current_comm(&ev.comm, sizeof(ev.comm));
    // ppid_comm and exe left empty (cross-skeleton map sharing not implemented)

    ev.saddr[0] = key->saddr;
    ev.daddr[0] = key->daddr;
    ev.sport = key->sport;
    ev.dport = key->dport;
    ev.family = key->family;
    ev.protocol = key->protocol;
    ev.state = val->state;
    ev.bytes_sent = val->bytes_sent;
    ev.bytes_recv = val->bytes_recv;
    ev.start_ns = val->start_ns;
    ev.last_ns = val->last_ns;

    __u64 *start = bpf_map_lookup_elem(&connect_start, &key->pid);
    if (start) {
        ev.connect_latency_ns = bpf_ktime_get_ns() - *start;
    }

    bpf_perf_event_output(ctx, &flow_events, BPF_F_CURRENT_CPU, &ev, sizeof(ev));
}

SEC("kprobe/tcp_sendmsg")
int BPF_KPROBE(tcp_sendmsg, struct sock *sk, struct msghdr *msg, size_t size)
{
    struct flow_key key = {};
    fill_flow_key(&key, sk);
    if (key.family != AF_INET) return 0;

    struct flow_val *val = bpf_map_lookup_elem(&flow_map, &key);
    if (!val) {
        struct flow_val new_val = {};
        new_val.start_ns = bpf_ktime_get_ns();
        bpf_map_update_elem(&flow_map, &key, &new_val, BPF_NOEXIST);
        val = bpf_map_lookup_elem(&flow_map, &key);
        if (!val) return 0;
    }
    val->bytes_sent += size;
    val->last_ns = bpf_ktime_get_ns();
    return 0;
}

SEC("kprobe/tcp_cleanup_rbuf")
int BPF_KPROBE(tcp_cleanup_rbuf, struct sock *sk, int copied)
{
    struct flow_key key = {};
    fill_flow_key(&key, sk);
    if (key.family != AF_INET) return 0;

    struct flow_val *val = bpf_map_lookup_elem(&flow_map, &key);
    if (!val) return 0;

    if (copied > 0) val->bytes_recv += copied;
    val->last_ns = bpf_ktime_get_ns();
    return 0;
}

SEC("kprobe/tcp_set_state")
int BPF_KPROBE(tcp_set_state, struct sock *sk, int state)
{
    struct flow_key key = {};
    fill_flow_key(&key, sk);
    if (key.family != AF_INET) return 0;

    struct flow_val *val = bpf_map_lookup_elem(&flow_map, &key);
    if (!val) return 0;

    val->state = state;
    val->last_ns = bpf_ktime_get_ns();

    // Track connect latency: SYN_SENT → ESTABLISHED
    __u32 pid = key.pid;
    if (state == TCP_SYN_SENT) {
        __u64 ts = bpf_ktime_get_ns();
        bpf_map_update_elem(&connect_start, &pid, &ts, BPF_ANY);
    }
    return 0;
}

SEC("kprobe/tcp_done")
int BPF_KPROBE(tcp_done, struct sock *sk)
{
    struct flow_key key = {};
    fill_flow_key(&key, sk);
    if (key.family != AF_INET) return 0;

    struct flow_val *val = bpf_map_lookup_elem(&flow_map, &key);
    if (!val) return 0;

    emit_flow(ctx, &key, val);
    bpf_map_delete_elem(&flow_map, &key);
    return 0;
}

char LICENSE[] SEC("license") = "GPL";
