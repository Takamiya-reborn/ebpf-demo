// SPDX-License-Identifier: GPL-2.0
// File Open Statistics - tracks file open/openat syscalls
// Monitors file open frequency per PID and per file
#include "vmlinux.h"
#include "bpf_helpers.h"
#include "bpf_tracing.h"

#define TASK_COMM_LEN 16
#define FNAME_LEN 32

// Per-PID open count
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 10240);
    __type(key, __u32);   // PID
    __type(value, __u64); // open count
} open_counter SEC(".maps");

// Per-process name open count (for comm-based aggregation)
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 10240);
    __type(key, char[TASK_COMM_LEN]); // comm
    __type(value, __u64);             // count
} open_comm_counter SEC(".maps");

// Per-PID failed open count (open returns -1)
struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 10240);
    __type(key, __u32);   // PID
    __type(value, __u64); // failed open count
} open_fail_counter SEC(".maps");

SEC("tracepoint/syscalls/sys_enter_openat")
int handle_openat_enter(struct trace_event_raw_sys_enter *ctx)
{
    __u32 pid = bpf_get_current_pid_tgid() >> 32;

    // Increment per-PID open count
    __u64 *val = bpf_map_lookup_elem(&open_counter, &pid);
    if (val) {
        __sync_fetch_and_add(val, 1);
    } else {
        __u64 one = 1;
        bpf_map_update_elem(&open_counter, &pid, &one, BPF_ANY);
    }

    // Increment per-comm count
    char comm[TASK_COMM_LEN];
    bpf_get_current_comm(&comm, sizeof(comm));
    __u64 *cval = bpf_map_lookup_elem(&open_comm_counter, &comm);
    if (cval) {
        __sync_fetch_and_add(cval, 1);
    } else {
        __u64 one = 1;
        bpf_map_update_elem(&open_comm_counter, &comm, &one, BPF_ANY);
    }

    return 0;
}

// Track open return value for failure detection
SEC("tracepoint/syscalls/sys_exit_openat")
int handle_openat_exit(struct trace_event_raw_sys_exit *ctx)
{
    // Negative return value indicates failure
    long ret = (long)(ctx->ret);
    if (ret < 0) {
        __u32 pid = bpf_get_current_pid_tgid() >> 32;

        __u64 *val = bpf_map_lookup_elem(&open_fail_counter, &pid);
        if (val) {
            __sync_fetch_and_add(val, 1);
        } else {
            __u64 one = 1;
            bpf_map_update_elem(&open_fail_counter, &pid, &one, BPF_ANY);
        }
    }

    return 0;
}

char LICENSE[] SEC("license") = "GPL";
