#include <linux/bpf.h>
#include <linux/types.h>
#include <linux/ptrace.h>
#include <linux/sched.h>
#include "bpf_helpers.h"
#include "bpf_tracing.h"

#define TASK_COMM_LEN 16

struct key_t {
	char comm[TASK_COMM_LEN];
};

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

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 10240);
	__type(key, struct key_t);
	__type(value, __u64);
} conn_exporter SEC(".maps");

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

	struct key_t k = {};
	bpf_get_current_comm(k.comm, sizeof(k.comm));

	__u64 *exp_val, one = 1;
	exp_val = bpf_map_lookup_elem(&conn_exporter, &k);
	if (exp_val) {
		__sync_fetch_and_add(exp_val, 1);
	} else {
		bpf_map_update_elem(&conn_exporter, &k, &one, BPF_ANY);
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