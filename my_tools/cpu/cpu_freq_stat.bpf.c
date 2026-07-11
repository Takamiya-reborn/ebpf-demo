// 1. 头文件
#include "vmlinux.h"
#include "bpf_helpers.h"
#include "bpf_tracing.h"

#define MAX_CPUS 256

struct freq_info {
	__u64 total_freq;
	__u64 count;
	__u64 min_freq;
	__u64 max_freq;
	__u64 cur_freq;
};

struct cpu_frequency_args {
	unsigned long long common_tp_fields;
	u32 state;
	u32 cpu_id;
};

struct {
	__uint(type, BPF_MAP_TYPE_ARRAY);
	__uint(max_entries, MAX_CPUS);
	__type(key, __u32);
	__type(value, struct freq_info);
} cpu_freq_map SEC(".maps");

SEC("tracepoint/power/cpu_frequency")
int handle_cpu_frequency(struct cpu_frequency_args *ctx)
{
	__u32 cpu = ctx->cpu_id;
	__u64 freq = ctx->state;

	if (cpu >= MAX_CPUS)
		return 0;

	struct freq_info *info = bpf_map_lookup_elem(&cpu_freq_map, &cpu);
	if (!info) {
		struct freq_info new_info = { .total_freq = freq,
					      .count = 1,
					      .min_freq = freq,
					      .max_freq = freq,
					      .cur_freq = freq };
		bpf_map_update_elem(&cpu_freq_map, &cpu, &new_info, BPF_ANY);
	} else {
		info->cur_freq = freq;
		__sync_fetch_and_add(&info->total_freq, freq);
		__sync_fetch_and_add(&info->count, 1);
		if (freq < info->min_freq)
			info->min_freq = freq;
		if (freq > info->max_freq)
			info->max_freq = freq;
	}
	return 0;
}

char LICENSE[] SEC("license") = "GPL";