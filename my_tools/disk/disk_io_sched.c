// Disk I/O Scheduler Latency - User-space loader
// Measures block I/O completion latency (issue to complete)
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <libbpf.h>
#include "disk_io_sched.skel.h"

static volatile bool exiting = false;

static void sig_handler(int sig)
{
    exiting = true;
}

int main(int argc, char **argv)
{
    struct disk_io_sched_bpf_linked *skel;
    int err;

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    skel = disk_io_sched_bpf_linked__open_and_load();
    if (!skel) {
        fprintf(stderr, "Failed to open and load BPF skeleton\n");
        return 1;
    }

    err = disk_io_sched_bpf_linked__attach(skel);
    if (err) {
        fprintf(stderr, "Failed to attach BPF programs\n");
        goto cleanup;
    }

    fprintf(stderr, "Disk I/O Scheduler Latency Monitor - Ctrl+C to stop\n");

    uint64_t prev_delay = 0, prev_count = 0;

    while (!exiting) {
        sleep(1);

        uint64_t cur_delay = 0, cur_count = 0;
        __u32 key;

        key = 0;
        __u64 val;
        if (bpf_map__lookup_elem(skel->maps.io_latency_stats, &key, sizeof(key),
                                 &val, sizeof(val), 0) == 0)
            cur_delay = val;

        key = 1;
        if (bpf_map__lookup_elem(skel->maps.io_latency_stats, &key, sizeof(key),
                                 &val, sizeof(val), 0) == 0)
            cur_count = val;

        uint64_t d_delay = cur_delay - prev_delay;
        uint64_t d_count = cur_count - prev_count;

        printf("disk_io_total_us: %.3f\n", d_delay / 1000.0);
        printf("disk_io_count: %llu\n", (unsigned long long)d_count);
        if (d_count > 0)
            printf("disk_io_avg_us: %.3f\n",
                   (d_delay / 1000.0) / d_count);

        prev_delay = cur_delay;
        prev_count = cur_count;
        fflush(stdout);
    }

cleanup:
    disk_io_sched_bpf_linked__destroy(skel);
    return err;
}
