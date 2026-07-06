// SPDX-License-Identifier: GPL-2.0
// CPU Usage - per-process on-CPU time tracking
// Uses sched_switch tracepoint to measure time spent on CPU
#include "vmlinux.h"
#include "bpf_helpers.h"
#include "bpf_tracing.h"

#define TASK_COMM_LEN 16

// Track when each PID started running on CPU
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 10240);
    __type(key, __u32);   // PID
    __type(value, __u64); // timestamp when scheduled on CPU
} cpu_start SEC(".maps");

// Cumulative on-CPU time per PID (in nanoseconds)
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 10240);
    __type(key, __u32);   // PID
    __type(value, __u64); // total CPU time (ns)
} cpu_time SEC(".maps");

// Per-CPU core usage tracking
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 256);
    __type(key, __u32);   // CPU ID
    __type(value, __u64); // total busy time (ns)
} cpu_busy SEC(".maps");

SEC("tracepoint/sched/sched_switch")
int handle_sched_switch(struct trace_event_raw_sched_switch *ctx)
{
    __u32 prev_pid = (__u32)(ctx->prev_pid);
    __u32 next_pid = (__u32)(ctx->next_pid);
    __u64 now = bpf_ktime_get_ns();
    __u32 cpu = bpf_get_smp_processor_id();

    // Account CPU time for the task being switched out
    if (prev_pid > 0) {
        __u64 *start = bpf_map_lookup_elem(&cpu_start, &prev_pid);
        if (start) {
            __u64 delta = now - *start;
            __u64 *total = bpf_map_lookup_elem(&cpu_time, &prev_pid);
            if (total) {
                __sync_fetch_and_add(total, delta);
            } else {
                bpf_map_update_elem(&cpu_time, &prev_pid, &delta, BPF_ANY);
            }

            // Track per-CPU busy time
            __u64 *busy = bpf_map_lookup_elem(&cpu_busy, &cpu);
            if (busy) {
                __sync_fetch_and_add(busy, delta);
            }
        }
        bpf_map_delete_elem(&cpu_start, &prev_pid);
    }

    // Record start time for the task being switched in
    if (next_pid > 0) {
        bpf_map_update_elem(&cpu_start, &next_pid, &now, BPF_ANY);
    }

    return 0;
}

char LICENSE[] SEC("license") = "GPL";
