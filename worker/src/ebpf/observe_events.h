#pragma once
// Shared structs between BPF programs and user-space collector.
// Must be kept in sync with the BPF side.

#ifndef __VMLINUX_H__
#include <linux/types.h>
#endif

#define MAX_COMM_LEN    16
#define MAX_FILENAME_LEN 128
#define MAX_IP_STR_LEN  16

// --- process_trace.bpf.c events ---
struct process_event {
    __u32 pid;
    __u32 ppid;
    char  comm[MAX_COMM_LEN];
    char  filename[MAX_FILENAME_LEN];
    __u32 event_type;  // 0=exec, 1=exit
    __u64 ts_ns;
};

// --- tcp_flow.bpf.c events ---
struct flow_event {
    __u32 pid;
    char  comm[MAX_COMM_LEN];
    char  ppid_comm[MAX_COMM_LEN];
    char  exe[MAX_FILENAME_LEN];
    __u32 saddr[4];  // IPv4 stored as __u32[4] for portability
    __u32 daddr[4];
    __u16 sport;
    __u16 dport;
    __u16 family;
    __u8  protocol;
    __u8  state;
    __u64 bytes_sent;
    __u64 bytes_recv;
    __u64 connect_latency_ns;
    __u64 start_ns;
    __u64 last_ns;
};
