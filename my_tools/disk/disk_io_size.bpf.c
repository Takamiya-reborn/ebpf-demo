// SPDX-License-Identifier: GPL-2.0
// Disk I/O Size - tracks block I/O request sizes
// Measures the size distribution of disk I/O operations
// Note: block_rq_issue uses struct trace_event_raw_block_rq in this kernel
#include "vmlinux.h"
#include "bpf_helpers.h"
#include "bpf_tracing.h"

// Stats keyed by operation type: 0=read_bytes, 1=write_bytes,
//   2=read_count, 3=write_count, 4=total_bytes, 5=total_count
struct {
    __uint(type, BPF_MAP_TYPE_ARRAY);
    __uint(max_entries, 8);
    __type(key, __u32);
    __type(value, __u64);
} io_size_stats SEC(".maps");

// Track per-PID I/O sizes
struct pid_io {
    __u64 read_bytes;
    __u64 write_bytes;
    __u64 read_count;
    __u64 write_count;
};

struct {
    __uint(type, BPF_MAP_TYPE_HASH);
    __uint(max_entries, 10240);
    __type(key, __u32);         // PID
    __type(value, struct pid_io);
} pid_io_stats SEC(".maps");

SEC("tracepoint/block/block_rq_issue")
int handle_block_rq_issue(struct trace_event_raw_block_rq *ctx)
{
    __u64 nr_sector = (__u64)(ctx->nr_sector);
    __u64 bytes = nr_sector * 512;

    // Determine read vs write from rwbs field (first char)
    int is_write = (ctx->rwbs[0] == 'W');

    __u32 idx_read_bytes = 0;
    __u32 idx_write_bytes = 1;
    __u32 idx_read_cnt = 2;
    __u32 idx_write_cnt = 3;
    __u32 idx_total_bytes = 4;
    __u32 idx_total_cnt = 5;

    __u64 *val;

    if (is_write) {
        val = bpf_map_lookup_elem(&io_size_stats, &idx_write_bytes);
        if (val) __sync_fetch_and_add(val, bytes);
        val = bpf_map_lookup_elem(&io_size_stats, &idx_write_cnt);
        if (val) __sync_fetch_and_add(val, 1);
    } else {
        val = bpf_map_lookup_elem(&io_size_stats, &idx_read_bytes);
        if (val) __sync_fetch_and_add(val, bytes);
        val = bpf_map_lookup_elem(&io_size_stats, &idx_read_cnt);
        if (val) __sync_fetch_and_add(val, 1);
    }

    val = bpf_map_lookup_elem(&io_size_stats, &idx_total_bytes);
    if (val) __sync_fetch_and_add(val, bytes);
    val = bpf_map_lookup_elem(&io_size_stats, &idx_total_cnt);
    if (val) __sync_fetch_and_add(val, 1);

    // Track per-PID I/O
    __u32 pid = bpf_get_current_pid_tgid() >> 32;
    struct pid_io *pio = bpf_map_lookup_elem(&pid_io_stats, &pid);
    if (!pio) {
        struct pid_io new_pio = {};
        if (is_write) {
            new_pio.write_bytes = bytes;
            new_pio.write_count = 1;
        } else {
            new_pio.read_bytes = bytes;
            new_pio.read_count = 1;
        }
        bpf_map_update_elem(&pid_io_stats, &pid, &new_pio, BPF_ANY);
    } else {
        if (is_write) {
            __sync_fetch_and_add(&pio->write_bytes, bytes);
            __sync_fetch_and_add(&pio->write_count, 1);
        } else {
            __sync_fetch_and_add(&pio->read_bytes, bytes);
            __sync_fetch_and_add(&pio->read_count, 1);
        }
    }

    return 0;
}

char LICENSE[] SEC("license") = "GPL";
