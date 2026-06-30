#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <stdint.h>
#include <libbpf.h>
#include "tcp_connect.skel.h"

#define TASK_COMM_LEN 16

struct conn_info {
	uint64_t count;
	char comm[TASK_COMM_LEN];
};

static volatile bool exiting = false;
static void sig_handler(int sig)
{
	exiting = true;
}

void print_and_clear_stats(struct tcp_connect_bpf_linked *skel)
{
	uint32_t key = 0, next_key;

	while (bpf_map__get_next_key(skel->maps.conn_count, &key, &next_key, sizeof(key)) == 0) {
		struct conn_info info;
		if (bpf_map__lookup_elem(skel->maps.conn_count, &next_key, sizeof(next_key), &info,
					 sizeof(info), 0) == 0) {
			printf("PID_%u_%s: %llu\n", next_key, info.comm,
			       (unsigned long long)info.count);
			bpf_map__delete_elem(skel->maps.conn_count, &next_key, sizeof(next_key), 0);
		}
		key = next_key;
	}
	fflush(stdout);
}

int main(int argc, char **argv)
{
	struct tcp_connect_bpf_linked *skel;
	int err;

	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);

	skel = tcp_connect_bpf_linked__open_and_load();
	if (!skel)
		return 1;

	err = tcp_connect_bpf_linked__attach(skel);
	if (err)
		goto cleanup;

	while (!exiting) {
		sleep(2);
		print_and_clear_stats(skel);
	}

cleanup:
	tcp_connect_bpf_linked__destroy(skel);
	return 0;
}