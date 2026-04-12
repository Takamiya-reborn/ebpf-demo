#include "vmlinux.h"
#include "bpf_helpers.h"
#include "bpf_tracing.h"
#include "bpf_core_read.h"

char LICENSE[] SEC("license") = "Dual BSD/GPL";

// 定义map：key是中断号irq，value是触发次数
struct my_irq_info {
	u64 count;
};

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 256); // 一般系统中断号不会超过256
	__type(key, int);
	__type(value, struct my_irq_info);
} irq_stats SEC(".maps");

// 挂载中断进入的tracepoint，这个是内核稳定ABI，所有5.4+内核都支持
SEC("tracepoint/irq/irq_handler_entry")
int handle_irq_entry(struct trace_event_raw_irq_handler_entry *ctx)
{
	int irq = ctx->irq;
	struct my_irq_info *info, zero = {};

	// 第一次触发的中断，初始化信息
	info = bpf_map_lookup_elem(&irq_stats, &irq);
	if (!info) {
		bpf_map_update_elem(&irq_stats, &irq, &zero, BPF_ANY);
		info = bpf_map_lookup_elem(&irq_stats, &irq);
		if (!info)
			return 0;
	}

	// 计数+1
	info->count += 1;
	return 0;
}