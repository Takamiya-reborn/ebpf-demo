#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <stdint.h>
#include <sys/socket.h>
#include <libbpf.h>
#include "socket_stat.skel.h"

#define TASK_COMM_LEN 16
struct family_info {
	uint64_t count;
};
struct pid_info {
	uint64_t count;
	char comm[TASK_COMM_LEN];
};

static volatile bool exiting = false;
static void sig_handler(int sig)
{
	exiting = true;
}

static const char *family_name(uint32_t family)
{
	switch (family) {
	case AF_INET:
		return "AF_INET";
	case AF_INET6:
		return "AF_INET6";
	case AF_UNIX:
		return "AF_UNIX";
	case AF_PACKET:
		return "AF_PACKET";
	default:
		return "OTHER";
	}
}

void print_and_clear_stats(struct socket_stat_bpf_linked *skel)
{
	uint32_t key = 0, next_key;

	// 协议族数据输出
	while (bpf_map__get_next_key(skel->maps.family_count, &key, &next_key, sizeof(key)) == 0) {
		struct family_info info;
		if (bpf_map__lookup_elem(skel->maps.family_count, &next_key, sizeof(next_key),
					 &info, sizeof(info), 0) == 0) {
			printf("Family_%s: %llu\n", family_name(next_key),
			       (unsigned long long)info.count);
			bpf_map__delete_elem(skel->maps.family_count, &next_key, sizeof(next_key),
					     0);
		}
		key = next_key;
	}

	// PID 进程数据输出
	key = 0;
	while (bpf_map__get_next_key(skel->maps.socket_pid_count, &key, &next_key, sizeof(key)) ==
	       0) {
		struct pid_info info;
		if (bpf_map__lookup_elem(skel->maps.socket_pid_count, &next_key, sizeof(next_key),
					 &info, sizeof(info), 0) == 0) {
			printf("PID_%u_%s: %llu\n", next_key, info.comm,
			       (unsigned long long)info.count);
			bpf_map__delete_elem(skel->maps.socket_pid_count, &next_key,
					     sizeof(next_key), 0);
		}
		key = next_key;
	}
	fflush(stdout);
}

int main(int argc, char **argv)
{
	struct socket_stat_bpf_linked *skel;
	signal(SIGINT, sig_handler);
	skel = socket_stat_bpf_linked__open_and_load();
	if (!skel)
		return 1;
	int err = socket_stat_bpf_linked__attach(skel);
	if (err)
		goto cleanup;

	while (!exiting) {
		sleep(2);
		print_and_clear_stats(skel);
	}
cleanup:
	socket_stat_bpf_linked__destroy(skel);
	return err;
}