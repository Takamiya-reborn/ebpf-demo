// SPDX-License-Identifier: GPL-2.0
// UDP Packet Statistics
// Tracks UDP send/recv operations per PID and overall UDP traffic patterns
#include "vmlinux.h"
#include "bpf_helpers.h"
#include "bpf_tracing.h"

#define TASK_COMM_LEN 16

// Per-PID UDP send statistics
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 10240);
    __type(key, __u32);   // PID
    __type(value, __u64); // send count
} udp_send_map SEC(".maps");

// Per-PID UDP receive statistics
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 10240);
    __type(key, __u32);   // PID
    __type(value, __u64); // recv count
} udp_recv_map SEC(".maps");

// Per-PID UDP send bytes
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 10240);
    __type(key, __u32);   // PID
    __type(value, __u64); // send bytes
} udp_send_bytes_map SEC(".maps");

// UDP global stats: 0=send_count, 1=recv_count, 2=send_bytes, 3=recv_bytes
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 8);
    __type(key, __u32);
    __type(value, __u64);
} udp_stats SEC(".maps");

SEC("kprobe/udp_sendmsg")
int handle_udp_sendmsg(struct pt_regs *ctx)
{
    __u32 pid = bpf_get_current_pid_tgid() >> 32;

    // Per-PID send count
    __u64 *val = bpf_map_lookup_elem(&udp_send_map, &pid);
    if (val) {
        __sync_fetch_and_add(val, 1);
    } else {
        __u64 one = 1;
        bpf_map_update_elem(&udp_send_map, &pid, &one, BPF_ANY);
    }

    // Global send count
    __u32 key_send_cnt = 0;
    val = bpf_map_lookup_elem(&udp_stats, &key_send_cnt);
    if (val) __sync_fetch_and_add(val, 1);
    else {
        __u64 one = 1;
        bpf_map_update_elem(&udp_stats, &key_send_cnt, &one, BPF_ANY);
    }

    // Track send size (len is the 3rd argument to udp_sendmsg)
    // struct sock *sk, struct msghdr *msg, size_t len
    size_t len = (size_t)PT_REGS_PARM3(ctx);
    __u32 key_send_bytes = 2;
    val = bpf_map_lookup_elem(&udp_stats, &key_send_bytes);
    if (val) __sync_fetch_and_add(val, len);
    else bpf_map_update_elem(&udp_stats, &key_send_bytes, &len, BPF_ANY);

    // Per-PID send bytes
    __u64 *bval = bpf_map_lookup_elem(&udp_send_bytes_map, &pid);
    if (bval) __sync_fetch_and_add(bval, len);
    else bpf_map_update_elem(&udp_send_bytes_map, &pid, &len, BPF_ANY);

    return 0;
}

SEC("kretprobe/udp_recvmsg")
int handle_udp_recvmsg(struct pt_regs *ctx)
{
    // Track recv size from return value
    int ret = (int)PT_REGS_RC(ctx);
    // 仅统计成功的接收；recvmsg 返回负值（失败）时不计入次数
    if (ret <= 0)
        return 0;

    __u32 pid = bpf_get_current_pid_tgid() >> 32;

    // Per-PID recv count
    __u64 *val = bpf_map_lookup_elem(&udp_recv_map, &pid);
    if (val) {
        __sync_fetch_and_add(val, 1);
    } else {
        __u64 one = 1;
        bpf_map_update_elem(&udp_recv_map, &pid, &one, BPF_ANY);
    }

    // Global recv count
    __u32 key_recv_cnt = 1;
    val = bpf_map_lookup_elem(&udp_stats, &key_recv_cnt);
    if (val) __sync_fetch_and_add(val, 1);
    else {
        __u64 one = 1;
        bpf_map_update_elem(&udp_stats, &key_recv_cnt, &one, BPF_ANY);
    }

    __u32 key_recv_bytes = 3;
    __u64 bytes = (__u64)ret;
    val = bpf_map_lookup_elem(&udp_stats, &key_recv_bytes);
    if (val) __sync_fetch_and_add(val, bytes);
    else bpf_map_update_elem(&udp_stats, &key_recv_bytes, &bytes, BPF_ANY);

    return 0;
}

char LICENSE[] SEC("license") = "GPL";
