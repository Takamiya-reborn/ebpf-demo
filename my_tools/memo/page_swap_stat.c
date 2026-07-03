#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include "libbpf.h"
#include "bpf.h"
#include "page_swap_stat.skel.h"

#define MAX_PIDS 65535
static uint64_t prev_values[MAX_PIDS] = { 0 };

int main(int argc, char **argv)
{
	struct page_swap_stat_bpf_linked *skel;
	int err;

	skel = page_swap_stat_bpf_linked__open();
	if (!skel) {
		fprintf(stderr, "Failed to open BPF skeleton\n");
		return 1;
	}

	err = page_swap_stat_bpf_linked__load(skel);
	if (err) {
		fprintf(stderr, "Failed to load BPF program\n");
		goto cleanup;
	}

	err = page_swap_stat_bpf_linked__attach(skel);
	if (err) {
		fprintf(stderr, "Failed to attach BPF program\n");
		goto cleanup;
	}

	fprintf(stderr, "Monitoring direct page reclaim delay by PID, press Ctrl+C to stop.\n");
	// 压力测试 python3 -c "a = ' ' * (1024 * 1024 * 1024 * 4); input('Press Enter to release')"
	while (1) {
		uint32_t pid = 0, next_pid;
		uint64_t current_total_ns;
		while (bpf_map__get_next_key(skel->maps.total_delay, &pid, &next_pid,
					     sizeof(pid)) == 0) {
			pid = next_pid;
			if (bpf_map__lookup_elem(skel->maps.total_delay, &pid, sizeof(pid),
						 &current_total_ns, sizeof(current_total_ns),
						 0) == 0) {
				uint64_t last = (pid < MAX_PIDS) ? prev_values[pid] : 0;
				if (current_total_ns > last) {
					uint64_t incremental_us = (current_total_ns - last) / 1000;
					// 输出后立即换行并刷新缓冲区
					printf("dr_pid_%u: %llu\n", pid,
					       (unsigned long long)incremental_us);
					fflush(stdout);
				}
				if (pid < MAX_PIDS)
					prev_values[pid] = current_total_ns;
			}
		}
		sleep(1);
	}

cleanup:
	page_swap_stat_bpf_linked__destroy(skel); // 修正
	return err;
}