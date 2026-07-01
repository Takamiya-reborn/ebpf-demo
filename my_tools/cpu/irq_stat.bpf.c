#include "vmlinux.h"
#include "bpf_helpers.h"

char LICENSE[] SEC("license") = "Dual BSD/GPL";

struct my_irq_info {
	u64 count;
};

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 512);
	__type(key, char[16]); // Key 改为进程名 (COMM)
	__type(value, struct my_irq_info);
} irq_stats SEC(".maps");

SEC("tracepoint/syscalls/sys_enter_execve")
int handle_execve(void *ctx)
{
	char comm[16];
	bpf_get_current_comm(&comm, sizeof(comm)); // 获取当前进程名

	struct my_irq_info *info, zero = { .count = 0 };

	info = bpf_map_lookup_elem(&irq_stats, &comm);
	if (!info) {
		bpf_map_update_elem(&irq_stats, &comm, &zero, BPF_ANY);
		info = bpf_map_lookup_elem(&irq_stats, &comm);
		if (!info)
			return 0;
	}

	__sync_fetch_and_add(&info->count, 1);
	return 0;
}