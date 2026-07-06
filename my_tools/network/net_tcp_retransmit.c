// TCP Retransmission Statistics - User-space loader
// Tracks TCP retransmission events per PID - key network stability indicator
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <libbpf.h>
#include "net_tcp_retransmit.skel.h"

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

int main(int argc, char **argv)
{
    struct net_tcp_retransmit_bpf_linked *skel;
    int err;

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    skel = net_tcp_retransmit_bpf_linked__open_and_load();
    if (!skel) {
        fprintf(stderr, "Failed to open and load BPF skeleton\n");
        return 1;
    }

    err = net_tcp_retransmit_bpf_linked__attach(skel);
    if (err) {
        fprintf(stderr, "Failed to attach BPF programs\n");
        goto cleanup;
    }

    fprintf(stderr, "TCP Retransmit Monitor started (waiting for events), Ctrl+C to stop\n");

    uint64_t prev_total = 0;

    while (!exiting) {
        sleep(1);

        // Global retransmit count (incremental only)
        __u32 key = 0;
        __u64 total;
        if (bpf_map__lookup_elem(skel->maps.retrans_stats, &key, sizeof(key),
                                 &total, sizeof(total), 0) == 0) {
            uint64_t delta = total - prev_total;
            if (delta > 0) {
                printf("tcp_retransmit_delta: %llu\n", (unsigned long long)delta);
            }
            prev_total = total;
        }

        // Per-PID retransmit counts
        __u32 pid = 0, next_pid;
        while (bpf_map__get_next_key(skel->maps.retrans_map, &pid, &next_pid,
                                     sizeof(next_pid)) == 0) {
            pid = next_pid;
            struct conn_info info;
            if (bpf_map__lookup_elem(skel->maps.retrans_map, &pid, sizeof(pid),
                                     &info, sizeof(info), 0) == 0) {
                if (info.count > 0) {
                    printf("tcp_retrans_pid_%u_%s: %llu\n", pid, info.comm,
                           (unsigned long long)info.count);
                }
                bpf_map__delete_elem(skel->maps.retrans_map, &pid, sizeof(pid), 0);
            }
        }

        // Per-comm retransmit counts
        char comm[TASK_COMM_LEN] = {};
        char next_comm[TASK_COMM_LEN];
        void *p_comm = NULL;
        __u64 count;

        while (bpf_map__get_next_key(skel->maps.retrans_comm_map, p_comm,
                                     next_comm, TASK_COMM_LEN) == 0) {
            if (bpf_map__lookup_elem(skel->maps.retrans_comm_map, next_comm,
                                     TASK_COMM_LEN, &count,
                                     sizeof(count), 0) == 0) {
                if (count > 0) {
                    printf("tcp_retrans_comm_%s: %llu\n", next_comm,
                           (unsigned long long)count);
                }
                bpf_map__delete_elem(skel->maps.retrans_comm_map, next_comm,
                                     TASK_COMM_LEN, 0);
            }
            memcpy(comm, next_comm, TASK_COMM_LEN);
            p_comm = comm;
        }

        fflush(stdout);
    }

cleanup:
    net_tcp_retransmit_bpf_linked__destroy(skel);
    return err;
}
