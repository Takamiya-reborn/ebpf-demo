#include "vmlinux.h"
#include "bpf_helpers.h"
#include "bpf_tracing.h"

#define TASK_COMM_LEN 16

struct family_info {
    __u64 count;
};

struct pid_info {
    __u64 count;
    char comm[TASK_COMM_LEN];
};

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 256);
    __type(key, __u32);
    __type(value, struct family_info);
} family_count SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 10240);
    __type(key, __u32);
    __type(value, struct pid_info);
} socket_pid_count SEC(".maps");

SEC("tracepoint/syscalls/sys_enter_socket")
int handle_socket_enter(struct trace_event_raw_sys_enter *ctx)
{
    __u32 family = (__u32)ctx->args[0];
    struct family_info *family_info;
    struct family_info family_zero = {};
    struct pid_info *pid_info;
    struct pid_info pid_zero = {};
    __u32 pid = bpf_get_current_pid_tgid() >> 32;

    family_info = bpf_map_lookup_elem(&family_count, &family);
    if (!family_info) {
        family_zero.count = 1;
        bpf_map_update_elem(&family_count, &family, &family_zero, BPF_ANY);
    } else {
        __sync_fetch_and_add(&family_info->count, 1);
    }

    pid_info = bpf_map_lookup_elem(&socket_pid_count, &pid);
    if (!pid_info) {
        bpf_get_current_comm(pid_zero.comm, sizeof(pid_zero.comm));
        pid_zero.count = 1;
        bpf_map_update_elem(&socket_pid_count, &pid, &pid_zero, BPF_ANY);
    } else {
        __sync_fetch_and_add(&pid_info->count, 1);
    }
    return 0;
}

char LICENSE[] SEC("license") = "GPL";
