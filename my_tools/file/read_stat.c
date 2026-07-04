#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <stdlib.h>
#include <libbpf.h>
#include "read_stat.skel.h"

#define MAX_ENTRIES 10240

int main(int argc, char **argv)
{
	struct read_stat_bpf_linked *skel = NULL;
	uint32_t *pids = NULL; // 1. 初始化为 NULL，消除警告
	int err = 0;

	skel = read_stat_bpf_linked__open();
	if (!skel) {
		fprintf(stderr, "Failed to open BPF skeleton\n");
		return 1;
	}

	err = read_stat_bpf_linked__load(skel);
	if (err) {
		fprintf(stderr, "Failed to load BPF program\n");
		goto cleanup;
	}

	err = read_stat_bpf_linked__attach(skel);
	if (err) {
		fprintf(stderr, "Failed to attach BPF program\n");
		goto cleanup;
	}

	// 2. 分配内存
	pids = malloc(sizeof(uint32_t) * MAX_ENTRIES);
	if (!pids) {
		fprintf(stderr, "Failed to allocate memory\n");
		err = -ENOMEM;
		goto cleanup;
	}

	fprintf(stderr, "Monitoring read syscalls, press Ctrl+C to stop.\n");

	while (1) {
		sleep(1);
		uint32_t pid = 0, next_pid;
		int count = 0;

		while (bpf_map__get_next_key(skel->maps.file_read_counter, &pid, &next_pid, sizeof(pid)) ==
		       0) {
			if (count >= MAX_ENTRIES)
				break;
			pids[count++] = next_pid;
			pid = next_pid;
		}

		for (int i = 0; i < count; i++) {
			uint32_t cur_pid = pids[i];
			uint64_t value;
			if (bpf_map__lookup_elem(skel->maps.file_read_counter, &cur_pid, sizeof(cur_pid),
						 &value, sizeof(value), 0) == 0) {
				// 适配你的 Python 正则格式
				printf("PID_%u: %llu\n", cur_pid, (unsigned long long)value);
				bpf_map__delete_elem(skel->maps.file_read_counter, &cur_pid, sizeof(cur_pid),
						     0);
			}
		}
		fflush(stdout);
	}

cleanup:
	if (pids)
		free(pids); // 3. 安全释放
	if (skel)
		read_stat_bpf_linked__destroy(skel);
	return err;
}