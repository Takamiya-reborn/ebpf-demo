// CPU Run Queue Delay - User-space loader
// Measures scheduler latency per PID: time from wakeup to actual on-CPU execution
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <libbpf.h>
#include "cpu_run_delay.skel.h"

#define MAX_PIDS 65536

static uint64_t prev_delay[MAX_PIDS] = {0};
static uint64_t prev_count[MAX_PIDS] = {0};
static volatile bool exiting = false;

static void sig_handler(int sig)
{
    exiting = true;
}

int main(int argc, char **argv)
{
    struct cpu_run_delay_bpf_linked *skel;
    int err;

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    skel = cpu_run_delay_bpf_linked__open_and_load();
    if (!skel) {
        fprintf(stderr, "Failed to open and load BPF skeleton\n");
        return 1;
    }

    err = cpu_run_delay_bpf_linked__attach(skel);
    if (err) {
        fprintf(stderr, "Failed to attach BPF programs\n");
        goto cleanup;
    }

    fprintf(stderr, "CPU Run Queue Delay Monitor - Ctrl+C to stop\n");

    while (!exiting) {
        sleep(1);

        __u32 pid = 0, next_pid;
        while (bpf_map__get_next_key(skel->maps.run_delay, &pid, &next_pid,
                                     sizeof(next_pid)) == 0) {
            pid = next_pid;
            __u64 total_ns;
            __u64 total_count = 0;
            int found = 0;

            if (bpf_map__lookup_elem(skel->maps.run_delay, &pid, sizeof(pid),
                                     &total_ns, sizeof(total_ns), 0) == 0) {
                found = 1;
            }

            if (bpf_map__lookup_elem(skel->maps.wakeup_count, &pid, sizeof(pid),
                                     &total_count, sizeof(total_count), 0) == 0) {
                // count found
            }

            if (found && pid < MAX_PIDS) {
                uint64_t delta_delay = total_ns > prev_delay[pid] ?
                    total_ns - prev_delay[pid] : 0;
                uint64_t delta_count = total_count > prev_count[pid] ?
                    total_count - prev_count[pid] : 0;

                if (delta_count > 0) {
                    uint64_t avg_delay_us = (delta_delay / delta_count) / 1000;
                    printf("runq_delay_pid_%u: %llu\n", pid,
                           (unsigned long long)avg_delay_us);
                }
                prev_delay[pid] = total_ns;
                prev_count[pid] = total_count;
            }
        }
        fflush(stdout);
    }

cleanup:
    cpu_run_delay_bpf_linked__destroy(skel);
    return err;
}
