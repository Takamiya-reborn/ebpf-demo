#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <libgen.h>
#include "irq_stat.skel.h"

typedef unsigned long long u64;

struct my_irq_info {
	u64 count;
};

static volatile bool exiting = false;

static void sig_handler(int sig)
{
	exiting = true;
}

int main(int argc, char **argv)
{
	struct irq_stat_bpf_linked *skel;
	int err;

	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);

	skel = irq_stat_bpf_linked__open_and_load();
	if (!skel) {
		fprintf(stderr, "Failed to load BPF skeleton\n");
		return 1;
	}

	err = irq_stat_bpf_linked__attach(skel);
	if (err) {
		fprintf(stderr, "Failed to attach BPF program: %d\n", err);
		goto cleanup;
	}

	fprintf(stderr, "Monitoring IRQs, press Ctrl+C to exit. Stats every 2 seconds...\n");
	while (!exiting) {
		sleep(2);
		int irq = -1, next_irq;
		struct my_irq_info info;

		while (bpf_map__get_next_key(skel->maps.irq_stats, &irq, &next_irq, sizeof(irq)) == 0) {
			irq = next_irq;
			if (bpf_map__lookup_elem(skel->maps.irq_stats, &irq, sizeof(irq), &info, sizeof(info), 0) == 0) {
				printf("irq_count_%d: %llu\n", irq, info.count);
			}
		}
	}

cleanup:
	irq_stat_bpf_linked__destroy(skel);
	printf("\nExited successfully\n");
	return err < 0 ? -err : 0;
}