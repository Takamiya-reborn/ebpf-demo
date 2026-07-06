// SPDX-License-Identifier: GPL-2.0
// Disk I/O Scheduler & Completion Latency
// Measures block I/O completion time (issue → complete) using tracepoints
// Uses trace_event_raw_block_rq for issue and trace_event_raw_block_rq_completion for complete
#include "vmlinux.h"
#include "bpf_helpers.h"
#include "bpf_tracing.h"

// Track I/O issue timestamp keyed by (PID << 32 | CPU)
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 10240);
    __type(key, __u64);   // key: (PID << 32) | CPU
    __type(value, __u64); // issue timestamp (ns)
} rq_issue_ts SEC(".maps");

// Cumulative stats: 0=total delay(ns), 1=count, 2=max delay
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 8);
    __type(key, __u32);
    __type(value, __u64);
} io_latency_stats SEC(".maps");

// Per-PID I/O latency
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 10240);
    __type(key, __u32);   // PID
    __type(value, __u64); // cumulative latency (ns)
} pid_io_latency SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 10240);
    __type(key, __u32);   // PID
    __type(value, __u64); // I/O count
} pid_io_count SEC(".maps");

SEC("tracepoint/block/block_rq_issue")
int handle_rq_issue(struct trace_event_raw_block_rq *ctx)
{
    __u32 pid = bpf_get_current_pid_tgid() >> 32;
    __u64 key = ((__u64)pid << 32) | (bpf_get_smp_processor_id());
    __u64 ts = bpf_ktime_get_ns();

    bpf_map_update_elem(&rq_issue_ts, &key, &ts, BPF_ANY);
    return 0;
}

SEC("tracepoint/block/block_rq_complete")
int handle_rq_complete(struct trace_event_raw_block_rq_completion *ctx)
{
    __u32 pid = bpf_get_current_pid_tgid() >> 32;
    __u64 key = ((__u64)pid << 32) | (bpf_get_smp_processor_id());

    __u64 *start = bpf_map_lookup_elem(&rq_issue_ts, &key);
    if (!start)
        return 0;

    __u64 now = bpf_ktime_get_ns();
    __u64 delta = now - *start;

    // Skip unreasonable values (> 10 seconds)
    if (delta > 10ULL * 1000 * 1000 * 1000) {
        bpf_map_delete_elem(&rq_issue_ts, &key);
        return 0;
    }

    __u32 idx_delay = 0;
    __u32 idx_count = 1;
    __u32 idx_max = 2;

    __u64 *val;

    // Update total delay
    val = bpf_map_lookup_elem(&io_latency_stats, &idx_delay);
    if (val) __sync_fetch_and_add(val, delta);
    else bpf_map_update_elem(&io_latency_stats, &idx_delay, &delta, BPF_ANY);

    // Update count
    val = bpf_map_lookup_elem(&io_latency_stats, &idx_count);
    if (val) __sync_fetch_and_add(val, 1);
    else {
        __u64 one = 1;
        bpf_map_update_elem(&io_latency_stats, &idx_count, &one, BPF_ANY);
    }

    // Update max
    val = bpf_map_lookup_elem(&io_latency_stats, &idx_max);
    if (val) {
        if (delta > *val)
            *val = delta;
    } else {
        bpf_map_update_elem(&io_latency_stats, &idx_max, &delta, BPF_ANY);
    }

    // Per-PID tracking
    __u64 *pid_lat = bpf_map_lookup_elem(&pid_io_latency, &pid);
    if (pid_lat) __sync_fetch_and_add(pid_lat, delta);
    else bpf_map_update_elem(&pid_io_latency, &pid, &delta, BPF_ANY);

    __u64 *pid_cnt = bpf_map_lookup_elem(&pid_io_count, &pid);
    if (pid_cnt) __sync_fetch_and_add(pid_cnt, 1);
    else {
        __u64 one = 1;
        bpf_map_update_elem(&pid_io_count, &pid, &one, BPF_ANY);
    }

    bpf_map_delete_elem(&rq_issue_ts, &key);
    return 0;
}

char LICENSE[] SEC("license") = "GPL";
