// SPDX-License-Identifier: GPL-2.0
// CPU Run Queue Delay - measures scheduler latency:
// the time between a task being woken up and actually starting to run on CPU
#include "vmlinux.h"
#include "bpf_helpers.h"
#include "bpf_tracing.h"

// Manually define sched_wakeup_new structure (not in this kernel's vmlinux.h)
struct sched_wakeup_new_args {
    struct trace_entry ent;
    char comm[16];
    pid_t pid;
    int prio;
    int target_cpu;
};

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 10240);
    __type(key, __u32);   // PID
    __type(value, __u64); // wakeup timestamp (ns)
} wakeup_ts SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 10240);
    __type(key, __u32);   // PID
    __type(value, __u64); // cumulative run queue delay (ns)
} run_delay SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 10240);
    __type(key, __u32);   // PID
    __type(value, __u64); // count of wakeups
} wakeup_count SEC(".maps");

// Track task wakeup timestamp
SEC("tracepoint/sched/sched_wakeup")
int handle_sched_wakeup(struct trace_event_raw_sched_wakeup_template *ctx)
{
    __u32 pid = (__u32)(ctx->pid);
    __u64 ts = bpf_ktime_get_ns();

    bpf_map_update_elem(&wakeup_ts, &pid, &ts, BPF_ANY);
    return 0;
}

// Also track new task wakeups (manually defined struct)
SEC("tracepoint/sched/sched_wakeup_new")
int handle_sched_wakeup_new(struct sched_wakeup_new_args *ctx)
{
    __u32 pid = (__u32)(ctx->pid);
    __u64 ts = bpf_ktime_get_ns();

    bpf_map_update_elem(&wakeup_ts, &pid, &ts, BPF_ANY);
    return 0;
}

// On context switch, calculate run queue delay for the task starting to run
SEC("tracepoint/sched/sched_switch")
int handle_sched_switch(struct trace_event_raw_sched_switch *ctx)
{
    __u32 next_pid = (__u32)(ctx->next_pid);
    __u64 *wakeup = bpf_map_lookup_elem(&wakeup_ts, &next_pid);
    if (!wakeup)
        return 0;

    __u64 now = bpf_ktime_get_ns();
    __u64 delay = now - *wakeup;

    // Accumulate run delay for this PID
    __u64 *total = bpf_map_lookup_elem(&run_delay, &next_pid);
    if (total) {
        __sync_fetch_and_add(total, delay);
    } else {
        bpf_map_update_elem(&run_delay, &next_pid, &delay, BPF_ANY);
    }

    // Increment wakeup count
    __u64 *count = bpf_map_lookup_elem(&wakeup_count, &next_pid);
    if (count) {
        __sync_fetch_and_add(count, 1);
    } else {
        __u64 one = 1;
        bpf_map_update_elem(&wakeup_count, &next_pid, &one, BPF_ANY);
    }

    // Clean up wakeup timestamp
    bpf_map_delete_elem(&wakeup_ts, &next_pid);
    return 0;
}

char LICENSE[] SEC("license") = "GPL";
