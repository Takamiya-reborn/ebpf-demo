#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <libbpf.h>
#include "bpf.h"
#include "page_fault_stat.skel.h"

int main(int argc, char **argv)
{
    struct page_fault_stat_bpf_linked *skel;
    int err;

    skel = page_fault_stat_bpf_linked__open();
    if (!skel) {
        fprintf(stderr, "Failed to open BPF skeleton\n");
        return 1;
    }

    err = page_fault_stat_bpf_linked__load(skel);
    if (err) {
        fprintf(stderr, "Failed to load BPF program\n");
        goto cleanup;
    }

    err = page_fault_stat_bpf_linked__attach(skel);
    if (err) {
        fprintf(stderr, "Failed to attach BPF program\n");
        goto cleanup;
    }

    uint32_t key_user = 0;
    uint32_t key_kernel = 1;
    uint64_t init_val = 0;
    bpf_map__update_elem(skel->maps.counter, &key_user, sizeof(key_user), &init_val, sizeof(init_val), BPF_ANY);
    bpf_map__update_elem(skel->maps.counter, &key_kernel, sizeof(key_kernel), &init_val, sizeof(init_val), BPF_ANY);

    printf("Monitoring page faults... Press Ctrl+C to stop.\n");

    while (1) {
        uint64_t value_user, value_kernel;
        err = bpf_map__lookup_elem(skel->maps.counter, &key_user, sizeof(key_user), &value_user, sizeof(value_user), 0);
        if (err) {
            fprintf(stderr, "Failed to lookup user page faults\n");
            break;
        }
        err = bpf_map__lookup_elem(skel->maps.counter, &key_kernel, sizeof(key_kernel), &value_kernel, sizeof(value_kernel), 0);
        if (err) {
            fprintf(stderr, "Failed to lookup kernel page faults\n");
            break;
        }
        printf("User page faults: %llu, Kernel page faults: %llu\n", (unsigned long long)value_user, (unsigned long long)value_kernel);
        sleep(1);
    }

cleanup:
    page_fault_stat_bpf_linked__destroy(skel);
    return err;
}