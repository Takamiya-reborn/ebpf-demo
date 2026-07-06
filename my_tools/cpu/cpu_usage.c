// CPU Usage - User-space loader
// Tracks per-process on-CPU time and overall CPU utilization
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <time.h>
#include <libbpf.h>
#include "cpu_usage.skel.h"

#define MAX_PIDS 65536
#define MAX_CPUS 256

static uint64_t prev_cpu_time[MAX_PIDS] = {0};
static uint64_t prev_cpu_busy[MAX_CPUS] = {0};
static volatile bool exiting = false;

static void sig_handler(int sig)
{
    exiting = true;
}

int main(int argc, char **argv)
{
    struct cpu_usage_bpf_linked *skel;
    int err;

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    skel = cpu_usage_bpf_linked__open_and_load();
    if (!skel) {
        fprintf(stderr, "Failed to open and load BPF skeleton\n");
        return 1;
    }

    err = cpu_usage_bpf_linked__attach(skel);
    if (err) {
        fprintf(stderr, "Failed to attach BPF programs\n");
        goto cleanup;
    }

    fprintf(stderr, "CPU Usage Monitor - Ctrl+C to stop\n");

    while (!exiting) {
        sleep(1);

        // Output per-PID CPU time
        __u32 pid = 0, next_pid;
        while (bpf_map__get_next_key(skel->maps.cpu_time, &pid, &next_pid,
                                     sizeof(next_pid)) == 0) {
            pid = next_pid;
            __u64 total_ns;
            if (bpf_map__lookup_elem(skel->maps.cpu_time, &pid, sizeof(pid),
                                     &total_ns, sizeof(total_ns), 0) == 0) {
                if (pid < MAX_PIDS) {
                    uint64_t delta = total_ns > prev_cpu_time[pid] ?
                        (total_ns - prev_cpu_time[pid]) / 1000 : 0; // us
                    if (delta > 0) {
                        printf("cpu_time_pid_%u: %llu\n", pid,
                               (unsigned long long)delta);
                    }
                    prev_cpu_time[pid] = total_ns;
                }
            }
        }

        // Output per-CPU busy time
        for (int cpu = 0; cpu < MAX_CPUS; cpu++) {
            __u64 busy_ns;
            __u32 key = cpu;
            if (bpf_map__lookup_elem(skel->maps.cpu_busy, &key, sizeof(key),
                                     &busy_ns, sizeof(busy_ns), 0) == 0) {
                uint64_t delta = busy_ns > prev_cpu_busy[cpu] ?
                    (busy_ns - prev_cpu_busy[cpu]) / 1000000 : 0; // ms
                if (delta > 0) {
                    printf("cpu_%d_busy_ms: %llu\n", cpu,
                           (unsigned long long)delta);
                }
                prev_cpu_busy[cpu] = busy_ns;
            }
        }
        fflush(stdout);
    }

cleanup:
    cpu_usage_bpf_linked__destroy(skel);
    return err;
}
