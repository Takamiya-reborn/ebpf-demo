// File fsync Statistics - User-space loader
// Tracks fsync/fdatasync latency: total time, count, and per-PID breakdown
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <libbpf.h>
#include "file_fsync_stat.skel.h"

static volatile bool exiting = false;

static void sig_handler(int sig)
{
    exiting = true;
}

int main(int argc, char **argv)
{
    struct file_fsync_stat_bpf_linked *skel;
    int err;

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    skel = file_fsync_stat_bpf_linked__open_and_load();
    if (!skel) {
        fprintf(stderr, "Failed to open and load BPF skeleton\n");
        return 1;
    }

    err = file_fsync_stat_bpf_linked__attach(skel);
    if (err) {
        fprintf(stderr, "Failed to attach BPF programs\n");
        goto cleanup;
    }

    fprintf(stderr, "fsync Latency Monitor - Ctrl+C to stop\n");

    uint64_t prev_latency = 0, prev_count = 0;

    while (!exiting) {
        sleep(1);

        // Global fsync stats
        __u32 key0 = 0, key1 = 1;
        __u64 cur_latency = 0, cur_count = 0;

        if (bpf_map__lookup_elem(skel->maps.fsync_stats, &key0, sizeof(key0),
                                 &cur_latency, sizeof(cur_latency), 0) != 0)
            cur_latency = 0;
        if (bpf_map__lookup_elem(skel->maps.fsync_stats, &key1, sizeof(key1),
                                 &cur_count, sizeof(cur_count), 0) != 0)
            cur_count = 0;

        uint64_t d_latency = cur_latency > prev_latency ?
            cur_latency - prev_latency : 0;
        uint64_t d_count = cur_count > prev_count ?
            cur_count - prev_count : 0;

        printf("fsync_total_us: %.3f\n", d_latency / 1000.0);
        printf("fsync_count: %llu\n", (unsigned long long)d_count);
        if (d_count > 0)
            printf("fsync_avg_us: %.3f\n",
                   (d_latency / 1000.0) / d_count);

        prev_latency = cur_latency;
        prev_count = cur_count;

        // Per-PID fsync latency
        __u32 pid = 0, next_pid;
        while (bpf_map__get_next_key(skel->maps.pid_fsync_latency, &pid, &next_pid,
                                     sizeof(next_pid)) == 0) {
            pid = next_pid;
            __u64 lat_ns;
            if (bpf_map__lookup_elem(skel->maps.pid_fsync_latency, &pid, sizeof(pid),
                                     &lat_ns, sizeof(lat_ns), 0) == 0) {
                printf("fsync_pid_%u_total_us: %.3f\n", pid, lat_ns / 1000.0);
            }
        }

        fflush(stdout);
    }

cleanup:
    file_fsync_stat_bpf_linked__destroy(skel);
    return err;
}
