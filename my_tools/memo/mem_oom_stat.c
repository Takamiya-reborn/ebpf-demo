// Memory OOM Statistics - User-space loader
// Tracks Out-Of-Memory killer events - a critical system stability indicator
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <libbpf.h>
#include "bpf.h"
#include "mem_oom_stat.skel.h"

#define TASK_COMM_LEN 16

struct oom_victim {
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
    struct mem_oom_stat_bpf_linked *skel;
    int err;

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    skel = mem_oom_stat_bpf_linked__open_and_load();
    if (!skel) {
        fprintf(stderr, "Failed to open and load BPF skeleton\n");
        return 1;
    }

    err = mem_oom_stat_bpf_linked__attach(skel);
    if (err) {
        fprintf(stderr, "Failed to attach BPF programs\n");
        goto cleanup;
    }

    fprintf(stderr, "OOM Kill Monitor started (waiting for events), Ctrl+C to stop\n");

    uint64_t prev_oom_count = 0;

    while (!exiting) {
        sleep(1);

        // Global OOM count (incremental only)
        __u32 key = 0;
        __u64 cur_oom_count = 0;
        if (bpf_map__lookup_elem(skel->maps.oom_stats, &key, sizeof(key),
                                 &cur_oom_count, sizeof(cur_oom_count), 0) == 0) {
            uint64_t delta = cur_oom_count - prev_oom_count;
            if (delta > 0) {
                printf("oom_kill_events: %llu\n", (unsigned long long)delta);
            }
            prev_oom_count = cur_oom_count;
        }

        // Per-victim OOM counts (incremental: delete after read)
        __u32 pid = 0, next_pid;
        while (bpf_map__get_next_key(skel->maps.oom_victim_map, &pid, &next_pid,
                                     sizeof(next_pid)) == 0) {
            pid = next_pid;
            struct oom_victim v;
            if (bpf_map__lookup_elem(skel->maps.oom_victim_map, &pid, sizeof(pid),
                                     &v, sizeof(v), 0) == 0) {
                if (v.count > 0) {
                    printf("oom_victim_pid_%u_%s: %llu\n", pid, v.comm,
                           (unsigned long long)v.count);
                }
                bpf_map__delete_elem(skel->maps.oom_victim_map, &pid, sizeof(pid), 0);
            }
        }

        fflush(stdout);
    }

cleanup:
    mem_oom_stat_bpf_linked__destroy(skel);
    return err;
}
