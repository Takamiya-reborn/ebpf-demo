#include <stdio.h>
#include <unistd.h>
#include <stdint.h>
#include <string.h>
#include "libbpf.h"
#include "bpf.h"
#include "page_fault_stat.skel.h"

struct val_t {
	char comm[16];
	uint64_t count;
};

int main(int argc, char **argv)
{
	struct page_fault_stat_bpf_linked *skel;
	int map_fd, err;

	skel = page_fault_stat_bpf_linked__open_and_load();
	if (!skel)
		return 1;

	err = page_fault_stat_bpf_linked__attach(skel);
	if (err)
		goto cleanup;

	map_fd = bpf_map__fd(skel->maps.counter);

	while (1) {
		uint32_t key, next_key;
		struct val_t val;
		int res;
		res = bpf_map_get_next_key(map_fd, NULL, &key);

		while (res == 0) {
			int next_res = bpf_map_get_next_key(map_fd, &key, &next_key);
			// 2. 查找当前 Key 的数值
			if (bpf_map_lookup_elem(map_fd, &key, &val) == 0) {
				if (val.count > 0) {
					printf("%s_%u: %llu\n", val.comm, key,
					       (unsigned long long)val.count);
				}
				bpf_map_delete_elem(map_fd, &key);
			}
			res = next_res;
			key = next_key;
		}
		fflush(stdout);
		sleep(1);
	}
cleanup:
	page_fault_stat_bpf_linked__destroy(skel);
	return err;
}