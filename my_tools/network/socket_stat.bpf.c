#include "vmlinux.h"
#include "bpf_helpers.h"
#include "bpf_tracing.h"

struct family_info {
    __u64 count;
};

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 256);
    __type(key, __u32);
    __type(value, struct family_info);
} family_count SEC(".maps");

SEC("tracepoint/syscalls/sys_enter_socket")
int handle_socket_enter(struct trace_event_raw_sys_enter *ctx)
{
    __u32 family = (__u32)ctx->args[0];
    struct family_info *info;
    struct family_info zero = {};

    info = bpf_map_lookup_elem(&family_count, &family);
    if (!info) {
        zero.count = 1;
        bpf_map_update_elem(&family_count, &family, &zero, BPF_ANY);
    } else {
        __sync_fetch_and_add(&info->count, 1);
    }
    return 0;
}

char LICENSE[] SEC("license") = "GPL";
