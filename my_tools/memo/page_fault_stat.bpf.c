#include <linux/bpf.h>
#include <linux/types.h>
#include "bpf_helpers.h"

struct val_t {
	char comm[16];
	__u64 count;
};

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 10240);
	__type(key, __u32);
	__type(value, struct val_t);
} counter SEC(".maps");

static __always_inline void count_page_fault()
{
	__u32 pid = bpf_get_current_pid_tgid() >> 32;
	struct val_t *val;
	struct val_t new_val = {};

	val = bpf_map_lookup_elem(&counter, &pid);
	if (val) {
		__sync_fetch_and_add(&val->count, 1);
	} else {
		new_val.count = 1;
		bpf_get_current_comm(&new_val.comm, sizeof(new_val.comm));
		bpf_map_update_elem(&counter, &pid, &new_val, BPF_NOEXIST);
	}
}

SEC("tracepoint/exceptions/page_fault_user")
int handle_page_fault_user(void *ctx)
{
	count_page_fault();
	return 0;
}

SEC("tracepoint/exceptions/page_fault_kernel")
int handle_page_fault_kernel(void *ctx)
{
	count_page_fault();
	return 0;
}

char LICENSE[] SEC("license") = "GPL";