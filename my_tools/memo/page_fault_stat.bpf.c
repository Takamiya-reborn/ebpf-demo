#include <linux/bpf.h>
#include <linux/types.h>
#include "bpf_helpers.h"

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 2);
    __type(key, __u32);
    __type(value, __u64);
} counter SEC(".maps");

SEC("tracepoint/exceptions/page_fault_user")
int handle_page_fault_user(void *ctx)
{
    __u32 key = 0; // user page faults
    __u64 *val = bpf_map_lookup_elem(&counter, &key);
    if (val) {
        __sync_fetch_and_add(val, 1);
    } else {
        __u64 init = 1;
        bpf_map_update_elem(&counter, &key, &init, BPF_ANY);
    }
    return 0;
}

SEC("tracepoint/exceptions/page_fault_kernel")
int handle_page_fault_kernel(void *ctx)
{
    __u32 key = 1; // kernel page faults
    __u64 *val = bpf_map_lookup_elem(&counter, &key);
    if (val) {
        __sync_fetch_and_add(val, 1);
    } else {
        __u64 init = 1;
        bpf_map_update_elem(&counter, &key, &init, BPF_ANY);
    }
    return 0;
}

char LICENSE[] SEC("license") = "GPL";