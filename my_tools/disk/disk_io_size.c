// Disk I/O Size - User-space loader
// Tracks block I/O request sizes: read/write bytes, counts, and averages
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <libbpf.h>
#include "disk_io_size.skel.h"

struct pid_io {
    uint64_t read_bytes;
    uint64_t write_bytes;
    uint64_t read_count;
    uint64_t write_count;
};

static volatile bool exiting = false;

static void sig_handler(int sig)
{
    exiting = true;
}

int main(int argc, char **argv)
{
    struct disk_io_size_bpf_linked *skel;
    int err;

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    skel = disk_io_size_bpf_linked__open_and_load();
    if (!skel) {
        fprintf(stderr, "Failed to open and load BPF skeleton\n");
        return 1;
    }

    err = disk_io_size_bpf_linked__attach(skel);
    if (err) {
        fprintf(stderr, "Failed to attach BPF programs\n");
        goto cleanup;
    }

    fprintf(stderr, "Disk I/O Size Monitor - Ctrl+C to stop\n");

    // Previous snapshot for differential output
    uint64_t prev[6] = {0};

    while (!exiting) {
        sleep(1);

        uint64_t cur[6] = {0};
        for (int i = 0; i < 6; i++) {
            __u32 key = i;
            __u64 val;
            if (bpf_map__lookup_elem(skel->maps.io_size_stats, &key, sizeof(key),
                                     &val, sizeof(val), 0) == 0) {
                cur[i] = val;
            }
        }

        // Output differential values
        uint64_t d_read_bytes = cur[0] - prev[0];
        uint64_t d_write_bytes = cur[1] - prev[1];
        uint64_t d_read_cnt = cur[2] - prev[2];
        uint64_t d_write_cnt = cur[3] - prev[3];
        uint64_t d_total_bytes = cur[4] - prev[4];
        uint64_t d_total_cnt = cur[5] - prev[5];

        if (d_total_cnt > 0) {
            printf("disk_read_bytes: %llu\n", (unsigned long long)d_read_bytes);
            printf("disk_write_bytes: %llu\n", (unsigned long long)d_write_bytes);
            printf("disk_read_count: %llu\n", (unsigned long long)d_read_cnt);
            printf("disk_write_count: %llu\n", (unsigned long long)d_write_cnt);
            printf("disk_total_bytes: %llu\n", (unsigned long long)d_total_bytes);
            printf("disk_total_count: %llu\n", (unsigned long long)d_total_cnt);
            if (d_read_cnt > 0)
                printf("disk_read_avg_kb: %.3f\n",
                       (d_read_bytes / 1024.0) / d_read_cnt);
            if (d_write_cnt > 0)
                printf("disk_write_avg_kb: %.3f\n",
                       (d_write_bytes / 1024.0) / d_write_cnt);
            printf("disk_io_avg_kb: %.3f\n",
                   d_total_cnt > 0 ? (d_total_bytes / 1024.0) / d_total_cnt : 0);
        }

        // Per-PID output
        __u32 pid = 0, next_pid;
        while (bpf_map__get_next_key(skel->maps.pid_io_stats, &pid, &next_pid,
                                     sizeof(next_pid)) == 0) {
            pid = next_pid;
            struct pid_io pio;
            if (bpf_map__lookup_elem(skel->maps.pid_io_stats, &pid, sizeof(pid),
                                     &pio, sizeof(pio), 0) == 0) {
                if (pio.read_count + pio.write_count > 0) {
                    printf("disk_io_pid_%u_read_bytes: %llu\n", pid,
                           (unsigned long long)pio.read_bytes);
                    printf("disk_io_pid_%u_write_bytes: %llu\n", pid,
                           (unsigned long long)pio.write_bytes);
                }
                bpf_map__delete_elem(skel->maps.pid_io_stats, &pid, sizeof(pid), 0);
            }
        }

        for (int i = 0; i < 6; i++)
            prev[i] = cur[i];

        fflush(stdout);
    }

cleanup:
    disk_io_size_bpf_linked__destroy(skel);
    return err;
}
