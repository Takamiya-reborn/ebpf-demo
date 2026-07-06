// SPDX-License-Identifier: GPL-2.0
// TCP Retransmission Statistics
// TCP retransmissions are a key network stability indicator:
// high retransmit rates indicate packet loss, congestion, or network issues
#include "vmlinux.h"
#include "bpf_helpers.h"
#include "bpf_tracing.h"

#define TASK_COMM_LEN 16

struct conn_info {
    __u64 count;
    char comm[TASK_COMM_LEN];
};

// Per-PID retransmit count
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 10240);
    __type(key, __u32);             // PID
    __type(value, struct conn_info);
} retrans_map SEC(".maps");

// Per-process-name retransmit count (for comm-based aggregation)
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 10240);
    __type(key, char[TASK_COMM_LEN]); // comm
    __type(value, __u64);             // count
} retrans_comm_map SEC(".maps");

// Global retransmit counter
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 4);
    __type(key, __u32);
    __type(value, __u64);
} retrans_stats SEC(".maps");

// tcp_retransmit_skb is called for every retransmitted TCP segment
SEC("kprobe/tcp_retransmit_skb")
int handle_tcp_retransmit(struct pt_regs *ctx)
{
    __u32 pid = bpf_get_current_pid_tgid() >> 32;

    // Increment global counter
    __u32 idx_total = 0;
    __u64 *val = bpf_map_lookup_elem(&retrans_stats, &idx_total);
    if (val) {
        __sync_fetch_and_add(val, 1);
    } else {
        __u64 one = 1;
        bpf_map_update_elem(&retrans_stats, &idx_total, &one, BPF_ANY);
    }

    // Per-PID tracking
    struct conn_info *info = bpf_map_lookup_elem(&retrans_map, &pid);
    if (info) {
        __sync_fetch_and_add(&info->count, 1);
    } else {
        struct conn_info new_info = {};
        new_info.count = 1;
        bpf_get_current_comm(&new_info.comm, TASK_COMM_LEN);
        bpf_map_update_elem(&retrans_map, &pid, &new_info, BPF_ANY);
    }

    // Per-comm tracking
    char comm[TASK_COMM_LEN];
    bpf_get_current_comm(&comm, sizeof(comm));
    __u64 *cval = bpf_map_lookup_elem(&retrans_comm_map, &comm);
    if (cval) {
        __sync_fetch_and_add(cval, 1);
    } else {
        __u64 one = 1;
        bpf_map_update_elem(&retrans_comm_map, &comm, &one, BPF_ANY);
    }

    return 0;
}

char LICENSE[] SEC("license") = "GPL";
