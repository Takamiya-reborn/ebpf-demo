#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <libbpf.h>
#include "bpf.h"
#include "disk_read_delay.skel.h"
#include "../common/kernel_utils.h"

static void init_stats(struct disk_read_delay_bpf_linked *skel)
{
    __u32 keys[] = {0, 1};
    __u64 value = 0;

    for (int i = 0; i < 2; i++) {
        bpf_map__update_elem(skel->maps.stats, &keys[i], sizeof(keys[i]), &value, sizeof(value), BPF_ANY);
    }
}

static void print_stats(struct disk_read_delay_bpf_linked *skel)
{
    __u32 total_key = 0;
    __u32 count_key = 1;
    __u64 total = 0, count = 0;

    if (bpf_map__lookup_elem(skel->maps.stats, &total_key, sizeof(total_key), &total, sizeof(total), 0) != 0) {
        fprintf(stderr, "Failed to lookup total latency\n");
        return;
    }
    if (bpf_map__lookup_elem(skel->maps.stats, &count_key, sizeof(count_key), &count, sizeof(count), 0) != 0) {
        fprintf(stderr, "Failed to lookup read count\n");
        return;
    }

    printf("disk_read_vfs_read_avg_latency_ns: %llu\n",
           count ? (unsigned long long)(total / count) : 0ULL);
    printf("disk_read_vfs_read_count: %llu\n",
           (unsigned long long)count);
}

int main(int argc, char **argv)
{
    struct disk_read_delay_bpf_linked *skel;
    int err;

    skel = disk_read_delay_bpf_linked__open();
    if (!skel) {
        fprintf(stderr, "Failed to open BPF skeleton\n");
        return 1;
    }

    err = disk_read_delay_bpf_linked__load(skel);
    if (err) {
        fprintf(stderr, "Failed to load BPF program\n");
        goto cleanup;
    }

    struct bpf_link *links[2] = {NULL, NULL};
    if (symbol_exists("vfs_read")) {
        links[0] = bpf_program__attach_kprobe(skel->progs.handle_vfs_read_enter, false, "vfs_read");
        if (!links[0]) {
            fprintf(stderr, "Failed to attach kprobe vfs_read\n");
            err = 1;
            goto cleanup;
        }
        links[1] = bpf_program__attach_kprobe(skel->progs.handle_vfs_read_return, true, "vfs_read");
        if (!links[1]) {
            fprintf(stderr, "Failed to attach kretprobe vfs_read\n");
            err = 1;
            goto cleanup;
        }
    } else {
        fprintf(stderr, "Warning: vfs_read symbol not found; skipping vfs_read probes.\n");
        links[0] = links[1] = NULL;
    }

    init_stats(skel);
    fprintf(stderr, "Monitoring vfs_read latency... Press Ctrl+C to stop.\n");

    while (1) {
        print_stats(skel);
        sleep(1);
    }

cleanup:
    for (int i = 0; i < 2; i++) {
        if (links[i])
            bpf_link__destroy(links[i]);
    }
    disk_read_delay_bpf_linked__destroy(skel);
    return err;
}
