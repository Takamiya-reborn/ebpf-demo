#include <stdio.h>
#include <unistd.h>
#include <signal.h>
#include <libgen.h>
#include "irq_stat.skel.h" // 编译时自动生成的骨架头

typedef unsigned long long u64;

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

	// 处理Ctrl+C退出信号
	signal(SIGINT, sig_handler);
	signal(SIGTERM, sig_handler);

	// 加载并挂载eBPF程序
	skel = irq_stat_bpf_linked__open_and_load();
	if (!skel) {
		fprintf(stderr, "❌ 加载BPF骨架失败，请检查编译是否正常\n");
		return 1;
	}

	err = irq_stat_bpf_linked__attach(skel);
	if (err) {
		fprintf(stderr, "❌ 挂载BPF程序失败，错误码: %d\n", err);
		goto cleanup;
	}

	printf("✅ 中断监控启动，按Ctrl+C退出，每2秒打印统计结果...\n");
	printf("%-8s %-12s\n", "IRQ号", "触发次数");
	printf("----------------------------------------\n");
	while (!exiting) {
		sleep(2);
		int irq = -1, next_irq;
		struct my_irq_info info;

		// 遍历map打印所有中断统计
		while (bpf_map__get_next_key(skel->maps.irq_stats, &irq, &next_irq, sizeof(irq)) == 0) {
			irq = next_irq;
			if (bpf_map__lookup_elem(skel->maps.irq_stats, &irq, sizeof(irq), &info, sizeof(info), 0) == 0) {
				printf("%-8d %-12llu\n", irq, info.count);
			}
		}
		printf("----------------------------------------\n");
	}

cleanup:
	irq_stat_bpf_linked__destroy(skel);
	printf("\n✅ 工具退出，环境验证成功！\n");
	return err < 0 ? -err : 0;
}