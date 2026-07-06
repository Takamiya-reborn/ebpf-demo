// UDP Packet Statistics - User-space loader
// Tracks UDP send/recv operations per PID with byte counts
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <libbpf.h>
#include "net_udp_stat.skel.h"

static volatile bool exiting = false;

static void sig_handler(int sig)
{
    exiting = true;
}

int main(int argc, char **argv)
{
    struct net_udp_stat_bpf_linked *skel;
    int err;

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    skel = net_udp_stat_bpf_linked__open_and_load();
    if (!skel) {
        fprintf(stderr, "Failed to open and load BPF skeleton\n");
        return 1;
    }

    err = net_udp_stat_bpf_linked__attach(skel);
    if (err) {
        fprintf(stderr, "Failed to attach BPF programs\n");
        goto cleanup;
    }

    fprintf(stderr, "UDP Traffic Monitor started (waiting for events), Ctrl+C to stop\n");

    uint64_t prev_send = 0, prev_recv = 0, prev_send_bytes = 0, prev_recv_bytes = 0;

    while (!exiting) {
        sleep(1);

        // Global UDP stats
        __u64 cur[4] = {0};
        for (int i = 0; i < 4; i++) {
            __u32 key = i;
            bpf_map__lookup_elem(skel->maps.udp_stats, &key, sizeof(key),
                                &cur[i], sizeof(cur[i]), 0);
        }

        uint64_t d_send = cur[0] - prev_send;
        uint64_t d_recv = cur[1] - prev_recv;
        uint64_t d_send_bytes = cur[2] - prev_send_bytes;
        uint64_t d_recv_bytes = cur[3] - prev_recv_bytes;

        // Only print when there's activity (incremental)
        if (d_send > 0)
            printf("udp_send_count: %llu\n", (unsigned long long)d_send);
        if (d_recv > 0)
            printf("udp_recv_count: %llu\n", (unsigned long long)d_recv);
        if (d_send_bytes > 0)
            printf("udp_send_bytes: %llu\n", (unsigned long long)d_send_bytes);
        if (d_recv_bytes > 0)
            printf("udp_recv_bytes: %llu\n", (unsigned long long)d_recv_bytes);

        prev_send = cur[0];
        prev_recv = cur[1];
        prev_send_bytes = cur[2];
        prev_recv_bytes = cur[3];

        // Per-PID UDP send counts
        __u32 pid = 0, next_pid;
        while (bpf_map__get_next_key(skel->maps.udp_send_map, &pid, &next_pid,
                                     sizeof(next_pid)) == 0) {
            pid = next_pid;
            __u64 count;
            if (bpf_map__lookup_elem(skel->maps.udp_send_map, &pid, sizeof(pid),
                                     &count, sizeof(count), 0) == 0) {
                if (count > 0) {
                    printf("udp_send_pid_%u: %llu\n", pid,
                           (unsigned long long)count);
                }
                bpf_map__delete_elem(skel->maps.udp_send_map, &pid, sizeof(pid), 0);
            }
        }

        // Per-PID UDP recv counts
        pid = 0;
        while (bpf_map__get_next_key(skel->maps.udp_recv_map, &pid, &next_pid,
                                     sizeof(next_pid)) == 0) {
            pid = next_pid;
            __u64 count;
            if (bpf_map__lookup_elem(skel->maps.udp_recv_map, &pid, sizeof(pid),
                                     &count, sizeof(count), 0) == 0) {
                if (count > 0) {
                    printf("udp_recv_pid_%u: %llu\n", pid,
                           (unsigned long long)count);
                }
                bpf_map__delete_elem(skel->maps.udp_recv_map, &pid, sizeof(pid), 0);
            }
        }

        // Per-PID UDP send bytes
        pid = 0;
        while (bpf_map__get_next_key(skel->maps.udp_send_bytes_map, &pid, &next_pid,
                                     sizeof(next_pid)) == 0) {
            pid = next_pid;
            __u64 bytes;
            if (bpf_map__lookup_elem(skel->maps.udp_send_bytes_map, &pid, sizeof(pid),
                                     &bytes, sizeof(bytes), 0) == 0) {
                if (bytes > 0) {
                    printf("udp_send_bytes_pid_%u: %llu\n", pid,
                           (unsigned long long)bytes);
                }
                bpf_map__delete_elem(skel->maps.udp_send_bytes_map, &pid,
                                     sizeof(pid), 0);
            }
        }

        fflush(stdout);
    }

cleanup:
    net_udp_stat_bpf_linked__destroy(skel);
    return err;
}
