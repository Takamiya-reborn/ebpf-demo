// File Open Statistics - User-space loader
// Tracks file open operations per PID, including failure counts
#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <stdint.h>
#include <string.h>
#include <libbpf.h>
#include "file_open_stat.skel.h"

#define TASK_COMM_LEN 16

static volatile bool exiting = false;

static void sig_handler(int sig)
{
    exiting = true;
}

int main(int argc, char **argv)
{
    struct file_open_stat_bpf_linked *skel;
    int err;

    signal(SIGINT, sig_handler);
    signal(SIGTERM, sig_handler);

    skel = file_open_stat_bpf_linked__open_and_load();
    if (!skel) {
        fprintf(stderr, "Failed to open and load BPF skeleton\n");
        return 1;
    }

    err = file_open_stat_bpf_linked__attach(skel);
    if (err) {
        fprintf(stderr, "Failed to attach BPF programs\n");
        goto cleanup;
    }

    fprintf(stderr, "File Open Statistics Monitor - Ctrl+C to stop\n");

    while (!exiting) {
        sleep(1);

        // Output per-PID open counts
        __u32 pid = 0, next_pid;
        while (bpf_map__get_next_key(skel->maps.open_counter, &pid, &next_pid,
                                     sizeof(next_pid)) == 0) {
            pid = next_pid;
            __u64 count;
            if (bpf_map__lookup_elem(skel->maps.open_counter, &pid, sizeof(pid),
                                     &count, sizeof(count), 0) == 0) {
                if (count > 0) {
                    printf("open_calls_pid_%u: %llu\n", pid,
                           (unsigned long long)count);
                }
                bpf_map__delete_elem(skel->maps.open_counter, &pid, sizeof(pid), 0);
            }
        }

        // Output failed open counts
        pid = 0;
        while (bpf_map__get_next_key(skel->maps.open_fail_counter, &pid, &next_pid,
                                     sizeof(next_pid)) == 0) {
            pid = next_pid;
            __u64 count;
            if (bpf_map__lookup_elem(skel->maps.open_fail_counter, &pid, sizeof(pid),
                                     &count, sizeof(count), 0) == 0) {
                if (count > 0) {
                    printf("open_fail_pid_%u: %llu\n", pid,
                           (unsigned long long)count);
                }
                bpf_map__delete_elem(skel->maps.open_fail_counter, &pid, sizeof(pid), 0);
            }
        }

        // Output per-comm counts
        char comm[TASK_COMM_LEN] = {};
        char next_comm[TASK_COMM_LEN];
        void *p_comm = NULL;
        __u64 comm_count;

        while (bpf_map__get_next_key(skel->maps.open_comm_counter, p_comm,
                                     next_comm, TASK_COMM_LEN) == 0) {
            if (bpf_map__lookup_elem(skel->maps.open_comm_counter, next_comm,
                                     TASK_COMM_LEN, &comm_count,
                                     sizeof(comm_count), 0) == 0) {
                if (comm_count > 0) {
                    printf("open_comm_%s: %llu\n", next_comm,
                           (unsigned long long)comm_count);
                }
                bpf_map__delete_elem(skel->maps.open_comm_counter, next_comm,
                                     TASK_COMM_LEN, 0);
            }
            memcpy(comm, next_comm, TASK_COMM_LEN);
            p_comm = comm;
        }

        fflush(stdout);
    }

cleanup:
    file_open_stat_bpf_linked__destroy(skel);
    return err;
}
