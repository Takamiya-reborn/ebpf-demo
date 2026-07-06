// SPDX-License-Identifier: GPL-2.0
// File fsync Statistics - tracks fsync/fdatasync latency
// Key performance indicator for data durability and I/O bottlenecks
#include "vmlinux.h"
#include "bpf_helpers.h"
#include "bpf_tracing.h"

// Track fsync entry timestamp per PID
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 10240);
    __type(key, __u32);   // PID
    __type(value, __u64); // entry timestamp (ns)
} fsync_start SEC(".maps");

// Cumulative stats: 0=total_latency(ns), 1=count, 2=fdatasync_total, 3=fdatasync_count
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 8);
    __type(key, __u32);
    __type(value, __u64);
} fsync_stats SEC(".maps");

// Per-PID fsync latency
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 10240);
    __type(key, __u32);   // PID
    __type(value, __u64); // cumulative latency (ns)
} pid_fsync_latency SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 10240);
    __type(key, __u32);   // PID
    __type(value, __u64); // fsync call count
} pid_fsync_count SEC(".maps");

static __always_inline void record_fsync_entry(void)
{
    __u32 pid = bpf_get_current_pid_tgid() >> 32;
    __u64 ts = bpf_ktime_get_ns();
    bpf_map_update_elem(&fsync_start, &pid, &ts, BPF_ANY);
}

static __always_inline void record_fsync_exit(__u32 idx)
{
    __u32 pid = bpf_get_current_pid_tgid() >> 32;
    __u64 *start = bpf_map_lookup_elem(&fsync_start, &pid);
    if (!start)
        return;

    __u64 delta = bpf_ktime_get_ns() - *start;

    // Update global stats
    __u64 *val;
    val = bpf_map_lookup_elem(&fsync_stats, &idx);
    if (val) __sync_fetch_and_add(val, delta);
    else bpf_map_update_elem(&fsync_stats, &idx, &delta, BPF_ANY);

    __u32 cnt_idx = idx + 1;
    val = bpf_map_lookup_elem(&fsync_stats, &cnt_idx);
    if (val) __sync_fetch_and_add(val, 1);
    else {
        __u64 one = 1;
        bpf_map_update_elem(&fsync_stats, &cnt_idx, &one, BPF_ANY);
    }

    // Per-PID tracking
    __u64 *plat = bpf_map_lookup_elem(&pid_fsync_latency, &pid);
    if (plat) __sync_fetch_and_add(plat, delta);
    else bpf_map_update_elem(&pid_fsync_latency, &pid, &delta, BPF_ANY);

    __u64 *pcnt = bpf_map_lookup_elem(&pid_fsync_count, &pid);
    if (pcnt) __sync_fetch_and_add(pcnt, 1);
    else {
        __u64 one = 1;
        bpf_map_update_elem(&pid_fsync_count, &pid, &one, BPF_ANY);
    }

    bpf_map_delete_elem(&fsync_start, &pid);
}

SEC("kprobe/vfs_fsync_range")
int handle_fsync_entry(struct pt_regs *ctx)
{
    record_fsync_entry();
    return 0;
}

SEC("kretprobe/vfs_fsync_range")
int handle_fsync_exit(struct pt_regs *ctx)
{
    record_fsync_exit(0); // 0=fsync total, 1=fsync count
    return 0;
}

char LICENSE[] SEC("license") = "GPL";
