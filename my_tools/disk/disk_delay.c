#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <libbpf.h>
#include "bpf.h"
#include "disk_delay.skel.h"

static void init_stats(struct disk_delay_bpf_linked *skel)
{
    __u32 keys[] = {0, 1, 2, 3};
    __u64 value = 0;

    for (int i = 0; i < 4; i++) {
        bpf_map__update_elem(skel->maps.stats, &keys[i], sizeof(keys[i]), &value, sizeof(value), BPF_ANY);
    }
}

static void print_stats(struct disk_delay_bpf_linked *skel)
{
    __u32 total_write_key = 0;
    __u32 count_write_key = 1;
    __u32 total_flush_key = 2;
    __u32 count_flush_key = 3;

    __u64 total_write = 0, count_write = 0;
    __u64 total_flush = 0, count_flush = 0;

    bpf_map__lookup_elem(skel->maps.stats, &total_write_key, sizeof(total_write_key), &total_write, sizeof(total_write), 0);
    bpf_map__lookup_elem(skel->maps.stats, &count_write_key, sizeof(count_write_key), &count_write, sizeof(count_write), 0);
    bpf_map__lookup_elem(skel->maps.stats, &total_flush_key, sizeof(total_flush_key), &total_flush, sizeof(total_flush), 0);
    bpf_map__lookup_elem(skel->maps.stats, &count_flush_key, sizeof(count_flush_key), &count_flush, sizeof(count_flush), 0);

    printf("\nDisk cache delay statistics:\n");
    if (count_write) {
        printf("  write-to-cache avg latency: %llu ns (count=%llu)\n",
               (unsigned long long)(total_write / count_write),
               (unsigned long long)count_write);
    } else {
        printf("  write-to-cache avg latency: N/A (no events)\n");
    }
    if (count_flush) {
        printf("  cache-to-disk avg latency: %llu ns (count=%llu)\n",
               (unsigned long long)(total_flush / count_flush),
               (unsigned long long)count_flush);
    } else {
        printf("  cache-to-disk avg latency: N/A (no events)\n");
    }
}

int main(int argc, char **argv)
{
    struct disk_delay_bpf_linked *skel;
    int err = 0;

    skel = disk_delay_bpf_linked__open();
    if (!skel) {
        fprintf(stderr, "Failed to open BPF skeleton\n");
        return 1;
    }

    err = disk_delay_bpf_linked__load(skel);
    if (err) {
        fprintf(stderr, "Failed to load BPF program\n");
        goto cleanup;
    }

    struct bpf_link *links[4] = {NULL, NULL, NULL, NULL};

    links[0] = bpf_program__attach_kprobe(skel->progs.handle_vfs_write, false, "vfs_write");
    if (!links[0]) {
        fprintf(stderr, "Failed to attach kprobe vfs_write\n");
        err = 1;
        goto cleanup;
    }

    links[1] = bpf_program__attach_kprobe(skel->progs.handle_ext4_file_write_iter, true, "ext4_file_write_iter");
    if (!links[1]) {
        links[1] = bpf_program__attach_kprobe(skel->progs.handle_generic_file_write_iter, true, "generic_file_write_iter");
        if (!links[1]) {
            fprintf(stderr, "Failed to attach kretprobe ext4_file_write_iter or generic_file_write_iter\n");
            err = 1;
            goto cleanup;
        }
    }

    links[2] = bpf_program__attach_kprobe(skel->progs.handle_blk_mq_start_request, false, "blk_mq_start_request");
    if (!links[2]) {
        fprintf(stderr, "Failed to attach kprobe blk_mq_start_request\n");
        err = 1;
        goto cleanup;
    }

    links[3] = bpf_program__attach_kprobe(skel->progs.handle_blk_update_request, false, "blk_update_request");
    if (!links[3]) {
        fprintf(stderr, "Failed to attach kprobe blk_update_request\n");
        err = 1;
        goto cleanup;
    }

    init_stats(skel);
    printf("Monitoring disk cache latencies... Press Ctrl+C to stop.\n");

    while (1) {
        print_stats(skel);
        sleep(1);
    }

cleanup:
    for (int i = 0; i < 4; i++) {
        if (links[i])
            bpf_link__destroy(links[i]);
    }
    disk_delay_bpf_linked__destroy(skel);
    return err;
}
