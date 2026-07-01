#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <stdint.h>
#include <inttypes.h>
#include "irq_stat.skel.h"

typedef uint64_t u64;
typedef uint32_t u32;
struct entry {
	char name[16];
	uint64_t count;
};

int compare_entries(const void *a, const void *b)
{
	return ((struct entry *)b)->count - ((struct entry *)a)->count;
}

struct my_irq_info {
	u64 count;
};

static volatile bool exiting = false;
static void sig_handler(int sig)
{
	exiting = true;
}

int main(int argc, char **argv)
{
	struct irq_stat_bpf_linked *skel;
	int err;

	signal(SIGINT, sig_handler);
	skel = irq_stat_bpf_linked__open_and_load();
	if (!skel)
		return 1;

	err = irq_stat_bpf_linked__attach(skel);
	if (err)
		goto cleanup;

	while (!exiting) {
		sleep(1);
		char next_key[16];
		char cur_key_storage[16];
		void *p_cur_key = NULL;
		struct my_irq_info info;
		struct entry data[512];
		int total_entries = 0;
		// 遍历并读取 Map
		while (bpf_map__get_next_key(skel->maps.irq_stats, p_cur_key, &next_key, 16) == 0) {
			if (bpf_map__lookup_elem(skel->maps.irq_stats, &next_key, 16, &info,
						 sizeof(info), 0) == 0) {
				strncpy(data[total_entries].name, next_key, 16);
				data[total_entries].count = info.count;
				total_entries++;
				// 清理已读取数据，实现差分统计
				bpf_map__delete_elem(skel->maps.irq_stats, &next_key, 16, 0);
			}
			memcpy(cur_key_storage, next_key, 16);
			p_cur_key = cur_key_storage;
		}
		// 排序
		qsort(data, total_entries, sizeof(struct entry), compare_entries);
		// 输出为 Python 易读的 JSON 格式
		int show_limit = 5;
		uint64_t others_count = 0;
		for (int i = 0; i < total_entries; i++) {
			if (i < show_limit) {
				printf("{\"%s\": %" PRIu64 "}\n", data[i].name, data[i].count);
			} else {
				others_count += data[i].count;
			}
		}
		if (others_count > 0) {
			printf("{\"others\": %" PRIu64 "}\n", others_count);
		}
		fflush(stdout);
	}

cleanup:
	irq_stat_bpf_linked__destroy(skel);
	return 0;
}