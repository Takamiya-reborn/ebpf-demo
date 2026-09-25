#include <stdio.h>
#include <unistd.h>
#include <libbpf.h>
#include <signal.h>
#include "disk_read_delay.skel.h"

struct stats_snapshot {
	__u64 vfs_ts, vfs_cnt, blk_ts, blk_cnt;
};
static struct stats_snapshot prev = { 0 };
static volatile bool exiting = false;
static void sig_handler(int sig)
{
	exiting = true;
}

int main()
{
	struct disk_read_delay_bpf_linked *skel = disk_read_delay_bpf_linked__open_and_load();
	if (!skel)
		return 1;
	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);
	skel->links.handle_vfs_read =
		bpf_program__attach_kprobe(skel->progs.handle_vfs_read, false, "vfs_read");
	skel->links.handle_vfs_read_ret =
		bpf_program__attach_kprobe(skel->progs.handle_vfs_read_ret, true, "vfs_read");
	skel->links.handle_blk_mq_start_request = bpf_program__attach_kprobe(
		skel->progs.handle_blk_mq_start_request, false, "blk_mq_start_request");
	skel->links.handle_blk_update_request = bpf_program__attach_kprobe(
		skel->progs.handle_blk_update_request, false, "blk_update_request");
	if (!skel->links.handle_vfs_read || !skel->links.handle_vfs_read_ret ||
	    !skel->links.handle_blk_mq_start_request || !skel->links.handle_blk_update_request) {
		fprintf(stderr, "Failed to attach one or more kprobes\n");
		disk_read_delay_bpf_linked__destroy(skel);
		return 1;
	}

	while (!exiting) {
		__u64 cur[4] = { 0 };
		__u64 delta[4] = { 0 };
		const __u64 prev_vals[4] = { prev.vfs_ts, prev.vfs_cnt, prev.blk_ts, prev.blk_cnt };
		for (int i = 0; i < 4; i++) {
			int err = bpf_map__lookup_elem(skel->maps.disk_read_stats, &i, sizeof(int),
						       &cur[i], sizeof(__u64), 0);
			if (err)
				fprintf(stderr, "failed to lookup stats key %d: %d\n", i, err);
			/* 累计值单调递增；读取失败或回退时按 0 增量处理，避免无符号下溢 */
			delta[i] = cur[i] > prev_vals[i] ? cur[i] - prev_vals[i] : 0;
		}
		printf("disk_read_vfs_total_us: %.3f\ndisk_read_vfs_count: %llu\n",
		       delta[0] / 1000.0, delta[1]);
		printf("disk_read_block_total_us: %.3f\ndisk_read_block_count: %llu\n",
		       delta[2] / 1000.0, delta[3]);
		fflush(stdout);
		prev = (struct stats_snapshot){ cur[0], cur[1], cur[2], cur[3] };
		sleep(1);
	}
	disk_read_delay_bpf_linked__destroy(skel);
	return 0;
}