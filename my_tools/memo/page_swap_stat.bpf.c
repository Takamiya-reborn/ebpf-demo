#include <linux/bpf.h>
#include <linux/types.h>
#include "bpf_helpers.h"

// 记录进程进入回收的时间戳
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 10240);
	__type(key, __u32);
	__type(value, __u64);
} start_ts SEC(".maps");

// 记录累计耗时（纳秒）
struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 10240);
	__type(key, __u32);
	__type(value, __u64);
} total_delay SEC(".maps");

SEC("tracepoint/vmscan/mm_vmscan_direct_reclaim_begin")
int handle_begin(void *ctx)
{
	__u32 pid = bpf_get_current_pid_tgid() >> 32;
	__u64 ts = bpf_ktime_get_ns();

	bpf_map_update_elem(&start_ts, &pid, &ts, BPF_ANY);
	return 0;
}

SEC("tracepoint/vmscan/mm_vmscan_direct_reclaim_end")
int handle_end(void *ctx)
{
	__u32 pid = bpf_get_current_pid_tgid() >> 32;
	__u64 *start = bpf_map_lookup_elem(&start_ts, &pid);

	if (start) {
		__u64 end_ts = bpf_ktime_get_ns();
		__u64 delta = end_ts - *start;

		__u64 *total = bpf_map_lookup_elem(&total_delay, &pid);
		if (!total) {
			bpf_map_update_elem(&total_delay, &pid, &delta, BPF_ANY);
		} else {
			__sync_fetch_and_add(total, delta);
		}
		bpf_map_delete_elem(&start_ts, &pid);
	}
	return 0;
}

char LICENSE[] SEC("license") = "GPL";