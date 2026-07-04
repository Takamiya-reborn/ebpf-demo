#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <libbpf.h>
#include <signal.h>
#include "disk_write_delay.skel.h"

struct stats_snapshot {
	__u64 total_write;
	__u64 count_write;
	__u64 total_flush;
	__u64 count_flush;
};

static struct stats_snapshot prev_stats = { 0 };

static void print_incremental_stats(struct disk_write_delay_bpf_linked *skel)
{
	__u32 total_write_key = 0, count_write_key = 1;
	__u32 total_flush_key = 2, count_flush_key = 3;
	__u64 curr_total_write = 0, curr_count_write = 0;
	__u64 curr_total_flush = 0, curr_count_flush = 0;

	// 从 Map 中读取当前累加值
	bpf_map__lookup_elem(skel->maps.disk_write_stats, &total_write_key, sizeof(__u32), &curr_total_write,
			     sizeof(__u64), 0);
	bpf_map__lookup_elem(skel->maps.disk_write_stats, &count_write_key, sizeof(__u32), &curr_count_write,
			     sizeof(__u64), 0);
	bpf_map__lookup_elem(skel->maps.disk_write_stats, &total_flush_key, sizeof(__u32), &curr_total_flush,
			     sizeof(__u64), 0);
	bpf_map__lookup_elem(skel->maps.disk_write_stats, &count_flush_key, sizeof(__u32), &curr_count_flush,
			     sizeof(__u64), 0);

	// 计算这一秒内的增量
	__u64 diff_write_ns = curr_total_write - prev_stats.total_write;
	__u64 diff_write_cnt = curr_count_write - prev_stats.count_write;
	__u64 diff_flush_ns = curr_total_flush - prev_stats.total_flush;
	__u64 diff_flush_cnt = curr_count_flush - prev_stats.count_flush;

	// 转换为毫秒 (double)
	double write_us = diff_write_ns / 1000.0;
	double flush_us = diff_flush_ns / 1000.0;

	printf("disk_write_vfs_total_us: %.3f\n", write_us);
	printf("disk_write_vfs_count: %llu\n", (unsigned long long)diff_write_cnt);
	printf("disk_write_block_total_us: %.3f\n", flush_us);
	printf("disk_write_block_count: %llu\n", (unsigned long long)diff_flush_cnt);

	fflush(stdout);

	// 更新快照
	prev_stats.total_write = curr_total_write;
	prev_stats.count_write = curr_count_write;
	prev_stats.total_flush = curr_total_flush;
	prev_stats.count_flush = curr_count_flush;
}

static volatile bool exiting = false;
static void sig_handler(int sig)
{
	exiting = true;
}

int main(int argc, char **argv)
{
	struct disk_write_delay_bpf_linked *skel;
	signal(SIGINT, sig_handler);

	skel = disk_write_delay_bpf_linked__open_and_load();
	if (!skel) {
		fprintf(stderr, "Failed to open and load BPF skeleton\n");
		return 1;
	}

	// 挂载探测点
	skel->links.handle_vfs_write =
		bpf_program__attach_kprobe(skel->progs.handle_vfs_write, false, "vfs_write");
	skel->links.handle_vfs_write_ret =
		bpf_program__attach_kprobe(skel->progs.handle_vfs_write_ret, true, "vfs_write");
	skel->links.handle_blk_mq_start_request = bpf_program__attach_kprobe(
		skel->progs.handle_blk_mq_start_request, false, "blk_mq_start_request");
	skel->links.handle_blk_update_request = bpf_program__attach_kprobe(
		skel->progs.handle_blk_update_request, false, "blk_update_request");

	fprintf(stderr, "Monitoring disk latencies (ms)... Press Ctrl+C to stop.\n");

	while (!exiting) {
		print_incremental_stats(skel);
		sleep(1);
	}

	disk_write_delay_bpf_linked__destroy(skel);
	return 0;
}