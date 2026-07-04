/* socket_monitor.bpf.c */
#include "vmlinux.h"
#include "bpf_helpers.h"
#include "bpf_tracing.h"

#define TASK_COMM_LEN 16

struct key_t {
	char comm[TASK_COMM_LEN];
};
struct family_info {
	__u64 count;
};
struct pid_info {
	__u64 count;
	char comm[TASK_COMM_LEN];
};

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 256);
	__type(key, __u32);
	__type(value, struct family_info);
} family_count SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 10240);
	__type(key, __u32);
	__type(value, struct pid_info);
} socket_pid_count SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 256);
	__type(key, __u32);
	__type(value, __u64);
} family_exporter SEC(".maps");

struct {
	__uint(type, BPF_MAP_TYPE_HASH);
	__uint(max_entries, 10240);
	__type(key, struct key_t);
	__type(value, __u64);
} socket_pid_exporter SEC(".maps");

SEC("tracepoint/syscalls/sys_enter_socket")
int handle_socket_enter(struct trace_event_raw_sys_enter *ctx)
{
	__u32 family = (__u32)ctx->args[0];
	__u32 pid = bpf_get_current_pid_tgid() >> 32;
	__u64 one = 1;

	struct family_info *f_info = bpf_map_lookup_elem(&family_count, &family);
	if (!f_info) {
		struct family_info f_z = { .count = 1 };
		bpf_map_update_elem(&family_count, &family, &f_z, BPF_ANY);
	} else {
		__sync_fetch_and_add(&f_info->count, 1);
	}

	struct pid_info *p_info = bpf_map_lookup_elem(&socket_pid_count, &pid);
	if (!p_info) {
		struct pid_info p_z = { .count = 1 };
		bpf_get_current_comm(p_z.comm, 16);
		bpf_map_update_elem(&socket_pid_count, &pid, &p_z, BPF_ANY);
	} else {
		__sync_fetch_and_add(&p_info->count, 1);
	}

	__u64 *f_exp = bpf_map_lookup_elem(&family_exporter, &family);
	if (f_exp)
		__sync_fetch_and_add(f_exp, 1);
	else
		bpf_map_update_elem(&family_exporter, &family, &one, BPF_ANY);

	struct key_t k = {};
	bpf_get_current_comm(k.comm, 16);
	__u64 *p_exp = bpf_map_lookup_elem(&socket_pid_exporter, &k);
	if (p_exp)
		__sync_fetch_and_add(p_exp, 1);
	else
		bpf_map_update_elem(&socket_pid_exporter, &k, &one, BPF_ANY);

	return 0;
}
char LICENSE[] SEC("license") = "GPL";