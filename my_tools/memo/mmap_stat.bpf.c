#include <linux/bpf.h>
#include <linux/types.h>
#include "bpf_helpers.h"

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 10240);
	__type(key, __u32);
	__type(value, __u64);
} memo_counter SEC(".maps");

SEC("tracepoint/syscalls/sys_enter_mmap")
int handle_enter_mmap(void *ctx)
{
	__u32 pid = bpf_get_current_pid_tgid() >> 32;
	__u64 *val;
	__u64 init = 1;

	// 使用原子操作进行累加
	val = bpf_map_lookup_elem(&memo_counter, &pid);
	if (!val) {
		// 如果不存在则插入1，如果已存在则不处理（由下方的atomic handle）
		bpf_map_update_elem(&memo_counter, &pid, &init, BPF_NOEXIST);
	} else {
		__sync_fetch_and_add(val, 1);
	}
	return 0;
}

char LICENSE[] SEC("license") = "GPL";