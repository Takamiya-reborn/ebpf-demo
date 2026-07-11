// CPU Frequency Statistics - User-space loader
// Monitors CPU frequency scaling events and outputs per-CPU frequency data
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <libbpf.h>
#include "cpu_freq_stat.skel.h"

#define MAX_CPUS 256

struct freq_info {
	__u64 total_freq;
	__u64 count;
	__u64 min_freq;
	__u64 max_freq;
	__u64 cur_freq;
};
static volatile bool exiting = false;

static void sig_handler(int sig)
{
	exiting = true;
}

int main(int argc, char **argv)
{
	struct cpu_freq_stat_bpf_linked *skel;
	int err;

	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);

	skel = cpu_freq_stat_bpf_linked__open_and_load();
	if (!skel) {
		fprintf(stderr, "Failed to open and load BPF skeleton\n");
		return 1;
	}

	err = cpu_freq_stat_bpf_linked__attach(skel);
	if (err) {
		fprintf(stderr, "Failed to attach BPF programs\n");
		goto cleanup;
	}

	fprintf(stderr, "CPU Frequency Monitor - Ctrl+C to stop\n");

	while (!exiting) {
		sleep(1);

		for (int cpu = 0; cpu < MAX_CPUS; cpu++) {
			__u32 key = cpu;
			struct freq_info info;
			if (bpf_map__lookup_elem(skel->maps.cpu_freq_map, &key, sizeof(key), &info,
						 sizeof(info), 0) == 0) {
				if (info.cur_freq > 0) {
					// Output frequency in MHz
					printf("cpu_%d_freq_mhz: %.3f\n", cpu,
					       info.cur_freq / 1000.0);
				}
			}
		}
		fflush(stdout);
	}

cleanup:
	cpu_freq_stat_bpf_linked__destroy(skel);
	return err;
}
