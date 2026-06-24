#include <linux/bpf.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include "bpf_helpers.h"
#include "bpf_tracing.h"

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 10240);
    __type(key, __u64);
    __type(value, __u64);
} ts_start SEC(".maps");

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 2);
    __type(key, __u32);
    __type(value, __u64);
} stats SEC(".maps");

static __always_inline void update_stat(__u32 key, __u64 delta)
{
    __u64 *val = bpf_map_lookup_elem(&stats, &key);
    if (val) {
        __sync_fetch_and_add(val, delta);
    } else {
        bpf_map_update_elem(&stats, &key, &delta, BPF_ANY);
    }
}

SEC("kprobe/vfs_read")
int handle_vfs_read_enter(struct pt_regs *ctx)
{
    __u64 tid = bpf_get_current_pid_tgid();
    __u64 ts = bpf_ktime_get_ns();
    bpf_map_update_elem(&ts_start, &tid, &ts, BPF_ANY);
    return 0;
}

SEC("kretprobe/vfs_read")
int handle_vfs_read_return(struct pt_regs *ctx)
{
    __u64 tid = bpf_get_current_pid_tgid();
    __u64 *start = bpf_map_lookup_elem(&ts_start, &tid);
    if (!start)
        return 0;

    __u64 delta = bpf_ktime_get_ns() - *start;
    update_stat(0, delta);
    update_stat(1, 1);
    bpf_map_delete_elem(&ts_start, &tid);
    return 0;
}

char LICENSE[] SEC("license") = "GPL";
