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
	skel->links.handle_vfs_read =
		bpf_program__attach_kprobe(skel->progs.handle_vfs_read, false, "vfs_read");
	skel->links.handle_vfs_read_ret =
		bpf_program__attach_kprobe(skel->progs.handle_vfs_read_ret, true, "vfs_read");
	skel->links.handle_blk_mq_start_request = bpf_program__attach_kprobe(
		skel->progs.handle_blk_mq_start_request, false, "blk_mq_start_request");
	skel->links.handle_blk_update_request = bpf_program__attach_kprobe(
		skel->progs.handle_blk_update_request, false, "blk_update_request");

	while (!exiting) {
		__u64 cur[4] = { 0 };
		for (int i = 0; i < 4; i++)
			bpf_map__lookup_elem(skel->maps.disk_read_stats, &i, sizeof(int), &cur[i],
					     sizeof(__u64), 0);
		printf("disk_read_vfs_total_us: %.3f\ndisk_read_vfs_count: %llu\n",
		       (cur[0] - prev.vfs_ts) / 1000.0, cur[1] - prev.vfs_cnt);
		printf("disk_read_block_total_us: %.3f\ndisk_read_block_count: %llu\n",
		       (cur[2] - prev.blk_ts) / 1000.0, cur[3] - prev.blk_cnt);
		fflush(stdout);
		prev = (struct stats_snapshot){ cur[0], cur[1], cur[2], cur[3] };
		sleep(1);
	}
	disk_read_delay_bpf_linked__destroy(skel);
	return 0;
}