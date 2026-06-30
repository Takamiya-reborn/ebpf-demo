#include <linux/bpf.h>
#include <linux/ptrace.h>
#include <linux/sched.h>
#include "bpf_helpers.h"
#include "bpf_tracing.h"

#define TASK_COMM_LEN 16

struct conn_info {
	__u64 count;
	char comm[TASK_COMM_LEN];
};

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 10240);
	__type(key, __u32);
	__type(value, struct conn_info);
} conn_count SEC(".maps");

static __always_inline int record_conn(void)
{
	__u32 pid = bpf_get_current_pid_tgid() >> 32;
	struct conn_info *info;
	struct conn_info zero = {};

	info = bpf_map_lookup_elem(&conn_count, &pid);
	if (!info) {
		bpf_get_current_comm(zero.comm, sizeof(zero.comm));
		zero.count = 1;
		bpf_map_update_elem(&conn_count, &pid, &zero, BPF_ANY);
	} else {
		__sync_fetch_and_add(&info->count, 1);
	}
	return 0;
}

SEC("kprobe/tcp_v4_connect")
int handle_tcp_v4_connect(struct pt_regs *ctx)
{
	return record_conn();
}

SEC("kprobe/tcp_v6_connect")
int handle_tcp_v6_connect(struct pt_regs *ctx)
{
	return record_conn();
}

char LICENSE[] SEC("license") = "GPL";