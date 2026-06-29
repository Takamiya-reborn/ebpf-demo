#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <libbpf.h>
#include "bpf.h"
#include "mmap_stat.skel.h"

int main(int argc, char **argv)
{
    struct mmap_stat_bpf_linked *skel;
    int err;

    skel = mmap_stat_bpf_linked__open();
    if (!skel) {
        fprintf(stderr, "Failed to open BPF skeleton\n");
        return 1;
    }

    err = mmap_stat_bpf_linked__load(skel);
    if (err) {
        fprintf(stderr, "Failed to load BPF program\n");
        goto cleanup;
    }

    err = mmap_stat_bpf_linked__attach(skel);
    if (err) {
        fprintf(stderr, "Failed to attach BPF program\n");
        goto cleanup;
    }

    fprintf(stderr, "Monitoring mmap syscalls by PID, press Ctrl+C to stop.\n");

    while (1) {
        uint32_t pid = 0, next_pid;
        uint64_t value;
        uint64_t total = 0;

        while (bpf_map__get_next_key(skel->maps.counter, &pid, &next_pid, sizeof(pid)) == 0) {
            pid = next_pid;
            if (bpf_map__lookup_elem(skel->maps.counter, &pid, sizeof(pid), &value, sizeof(value), 0) == 0) {
                printf("mmap_calls_pid_%u: %llu\n", pid, (unsigned long long)value);
                total += value;
            }
        }
        printf("mmap_calls_total: %llu\n", (unsigned long long)total);
        sleep(1);
    }

cleanup:
    mmap_stat_bpf_linked__destroy(skel);
    return err;
}