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
} ts_start_pid SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 10240);
	__type(key, __u64);
	__type(value, __u64);
} ts_start_req SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 4);
	__type(key, __u32);
	__type(value, __u64);
} disk_write_stats SEC(".maps");

static __always_inline void update_stat(__u32 key, __u64 delta)
{
	__u64 *val = bpf_map_lookup_elem(&disk_write_stats, &key);
	if (val) {
		__sync_fetch_and_add(val, delta);
	} else {
		bpf_map_update_elem(&disk_write_stats, &key, &delta, BPF_ANY);
	}
}

SEC("kprobe/vfs_write")
int handle_vfs_write(struct pt_regs *ctx)
{
	__u64 tid = bpf_get_current_pid_tgid();
	__u64 ts = bpf_ktime_get_ns();
	bpf_map_update_elem(&ts_start_pid, &tid, &ts, BPF_ANY);
	return 0;
}

SEC("kretprobe/vfs_write")
int handle_vfs_write_ret(struct pt_regs *ctx)
{
	__u64 tid = bpf_get_current_pid_tgid();
	__u64 *start = bpf_map_lookup_elem(&ts_start_pid, &tid);
	if (!start)
		return 0;

	__u64 delta = bpf_ktime_get_ns() - *start;
	__u32 total_key = 0; // total_write_latency
	__u32 count_key = 1; // count_write

	update_stat(total_key, delta);
	update_stat(count_key, 1);

	bpf_map_delete_elem(&ts_start_pid, &tid);
	return 0;
}

SEC("kprobe/blk_mq_start_request")
int handle_blk_mq_start_request(struct pt_regs *ctx)
{
	__u64 rq = (__u64)PT_REGS_PARM1(ctx);
	__u64 ts = bpf_ktime_get_ns();
	bpf_map_update_elem(&ts_start_req, &rq, &ts, BPF_ANY);
	return 0;
}

SEC("kprobe/blk_update_request")
int handle_blk_update_request(struct pt_regs *ctx)
{
	__u64 rq = (__u64)PT_REGS_PARM1(ctx);
	__u64 *start = bpf_map_lookup_elem(&ts_start_req, &rq);
	if (!start)
		return 0;

	__u64 delta = bpf_ktime_get_ns() - *start;
	__u32 total_key = 2; // total_flush_latency
	__u32 count_key = 3; // count_flush

	update_stat(total_key, delta);
	update_stat(count_key, 1);
	bpf_map_delete_elem(&ts_start_req, &rq);
	return 0;
}

char LICENSE[] SEC("license") = "GPL";