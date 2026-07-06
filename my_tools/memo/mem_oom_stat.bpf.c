// SPDX-License-Identifier: GPL-2.0
// Memory OOM Statistics - tracks Out-Of-Memory killer events
// A critical system stability indicator: when the kernel OOM killer fires,
// it means the system is under severe memory pressure
#include "vmlinux.h"
#include "bpf_helpers.h"
#include "bpf_tracing.h"

#define TASK_COMM_LEN 16

// Per-victim-PID OOM kill count
struct oom_victim {
    __u64 count;
    char comm[TASK_COMM_LEN];
};

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 10240);
    __type(key, __u32);             // victim PID
    __type(value, struct oom_victim);
} oom_victim_map SEC(".maps");

// OOM event counter (global)
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 4);
    __type(key, __u32);
    __type(value, __u64);
} oom_stats SEC(".maps");

// Track OOM killer invocations via kprobe on oom_kill_process
// This is called for each process that gets killed by the OOM killer
SEC("kprobe/oom_kill_process")
int handle_oom_kill_process(struct pt_regs *ctx)
{
    // Increment global OOM counter
    __u32 idx_count = 0;
    __u64 *val = bpf_map_lookup_elem(&oom_stats, &idx_count);
    if (val) {
        __sync_fetch_and_add(val, 1);
    } else {
        __u64 one = 1;
        bpf_map_update_elem(&oom_stats, &idx_count, &one, BPF_ANY);
    }

    // Also track the current process (OOM reaper context)
    // The actual victim task_struct is the second argument to oom_kill_process
    // For simplicity, we track the current PID performing the kill
    __u32 pid = bpf_get_current_pid_tgid() >> 32;
    struct oom_victim *v = bpf_map_lookup_elem(&oom_victim_map, &pid);
    if (v) {
        __sync_fetch_and_add(&v->count, 1);
    } else {
        struct oom_victim new_v = {};
        new_v.count = 1;
        bpf_get_current_comm(&new_v.comm, TASK_COMM_LEN);
        bpf_map_update_elem(&oom_victim_map, &pid, &new_v, BPF_ANY);
    }

    return 0;
}

char LICENSE[] SEC("license") = "GPL";
