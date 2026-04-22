#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <libbpf.h>
#include "bpf.h"
#include "oom_stat.skel.h"

int main(int argc, char **argv)
{
    struct oom_stat_bpf_linked *skel;
    int err;

    skel = oom_stat_bpf_linked__open();
    if (!skel) {
        fprintf(stderr, "Failed to open BPF skeleton\n");
        return 1;
    }

    err = oom_stat_bpf_linked__load(skel);
    if (err) {
        fprintf(stderr, "Failed to load BPF program\n");
        goto cleanup;
    }

    err = oom_stat_bpf_linked__attach(skel);
    if (err) {
        fprintf(stderr, "Failed to attach BPF program\n");
        goto cleanup;
    }

    uint32_t key = 0;
    uint64_t init_val = 0;
    bpf_map__update_elem(skel->maps.counter, &key, sizeof(key), &init_val, sizeof(init_val), BPF_ANY);

    printf("Monitoring OOM events... Press Ctrl+C to stop.\n");

    while (1) {
        uint32_t key = 0;
        uint64_t value;
        err = bpf_map__lookup_elem(skel->maps.counter, &key, sizeof(key), &value, sizeof(value), 0);
        if (err) {
            fprintf(stderr, "Failed to lookup map\n");
            break;
        }
        printf("OOM kills: %llu\n", (unsigned long long)value);
        sleep(1);
    }

cleanup:
    oom_stat_bpf_linked__destroy(skel);
    return err;
}