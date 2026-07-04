# my_tools — 基于 libbpf 的系统分析工具集合（更新）

本文件基于源码审阅生成，反映当前 `my_tools` 的实际工具、运行方式和输出格式。目标读者：开发者、运维和 `client` 前端集成维护者。

**重要**：`client`（[client/main.py](../../client/main.py#L1)）会扫描 `my_tools/bin/` 并以 `sudo` 无交互方式执行工具，期望工具输出为可解析的 `key: value` 或单行 JSON 字典。

## 目录摘要

- `Makefile`：按分类（`cpu/disk/file/memo/network`）构建 BPF 程序与用户态加载器，输出到 `bin/`。构建流程使用 `clang -target bpf` 编译 `.bpf.c`，并用 `bpftool` 生成 skeleton。
- `common/`：公共头文件（`vmlinux.h`、`kernel_utils.h`），供 BPF 程序和用户态引用。
- `bin/`：构建产物，包含可直接执行的用户态工具。
- 分类源码目录：包含每个工具的 `.bpf.c`（内核 BPF 程序）和 `.c`（用户态加载器）。

当前可用工具（位于 `my_tools/bin/`）：

- `irq_stat` — CPU / IRQ 统计
- `disk_read_delay` — 磁盘读延迟（增量/秒）
- `disk_write_delay` — 磁盘写延迟（增量/秒）
- `mmap_stat` — `mmap()` 调用统计（按 PID 增量）
- `page_fault_stat` — 页面错误统计（按进程）
- `page_swap_stat` — 页面交换延迟（按 PID 增量，单位 us）
- `read_stat` — `read()` 调用统计（按 PID 增量）
- `write_stat` — `write()` 调用统计（按 PID 增量）
- `socket_stat` — `socket()` / 协议族与进程统计
- `tcp_connect` — TCP 连接请求统计（按 PID / comm）

## 构建与运行

在 `my_tools` 根目录执行：

```bash
cd my_tools
make          # 构建所有分类
# 或按类别构建，例如：
make network
```

构建完成后，运行示例：

```bash
sudo ./bin/irq_stat
sudo ./bin/tcp_connect
```

注意：多数工具会以 `sleep(1)` 或 `sleep(2)` 的周期轮询 BPF maps 并输出增量/快照；有些工具在输出后会清理 map（以实现差分统计）。

## 输出格式规范（按现有实现总结）

client 使用 `parse_output_metrics()` 对工具输出做解析，支持两类格式：

- 单行 JSON 字典，例如 `{"metrics": 123}`；
- 简洁的 `key: value` 或 `key=value` 格式（行内只有一个数值），以及以 `PID_...` 或 `write_calls_pid_...` 形式标签化的数值行。

工具实现中的常见输出示例：

- irq_stat：多行 JSON，每行为 `{"IRQ_NAME": count}` 或 `{"others": count}`。
- disk_*：带单位后缀的 key，例如 `disk_write_vfs_total_us: 12.345`、`disk_write_vfs_count: 3`。
- read_stat / write_stat / mmap_stat / tcp_connect / page_swap_stat：以进程为维度的行，例如 `PID_1234: 56`、`write_calls_pid_1234: 7`、`mmap_calls_pid_1234: 2`、`PID_123_comm: 9`。
- socket_stat：协议族与进程两部分，示例 `Family_AF_INET: 10`、`PID_1234_comm: 5`。

建议：保持每行只包含一个易解析的数值项，输出行以 `key[:=] value` 或单行 JSON 字典结束。

## 各工具运行与实现要点（源码摘录后的摘要）

- `irq_stat` (`cpu/irq_stat.c`)
  - 加载 `irq_stat.bpf.c` skeleton，附加 tracepoint 并每秒遍历 `irq_stats` map。
  - 输出前 N 条（默认 5）为 JSON 行，超出项合并为 `{"others": N}`。

- `disk_write_delay` / `disk_read_delay` (`disk/*.c`)
  - 通过 kprobe 附加 `vfs_write` / `vfs_read` 以及 block 层相关函数，maps 聚合累计耗时与计数。
  - 用户态每秒读取累加值并与上次快照差分，输出如 `disk_write_vfs_total_us`（浮点）与 `disk_write_vfs_count`（整数）。

- `read_stat` / `write_stat` (`file/*.c`)
  - BPF map 按 PID 计数，用户态遍历 map 并输出 `PID_<pid>: <count>` 或 `write_calls_pid_<pid>: <count>`。
  - 用户态通常在读取后删除 map 中的键以实现增量统计。

- `mmap_stat` / `page_fault_stat` / `page_swap_stat` (`memo/*.c`)
  - `mmap_stat`：按 PID 输出增量 `mmap_calls_pid_<pid>` 并尝试将值清零以实现增量。
  - `page_fault_stat`：以 `comm_pid: count` 格式输出并删除条目。
  - `page_swap_stat`：保存本地 `prev_values[]` 快照，输出 `dr_pid_<pid>: <incremental_us>`。

- `socket_stat` / `tcp_connect` (`network/*.c`)
  - `socket_stat` 输出协议族统计 `Family_<NAME>: <count>` 以及 `PID_<pid>_<comm>: <count>`。
  - `tcp_connect` 输出 `PID_<pid>_<comm>: <count>` 并在输出后删除 map 条目。

## 集成注意事项与已识别问题

- `client/main.py` 的 `parse_output_metrics()` 正则表达式匹配有限：它仅识别单个浮点或整数值行，或可解析为字典的 JSON 行。若你修改工具输出，务必保持兼容或同时更新 `client` 的解析逻辑（参见 [client/main.py](../../client/main.py#L1) 的 `parse_output_metrics` 实现）。
- 若工具在用户态使用 `bpf_map__delete_elem()` 清理 map，`client` 若实时读取工具 stdout，应注意可能存在短时间内无数据的窗口。
- `Makefile` 中将 `clang -target bpf` 与 `bpftool` 用于生成 skeletons；确保构建机器上已安装 `clang`、`bpftool` 和 `libbpf` 的开发头文件。

## 文档已更新的变更点（相对于旧版）

- 用源码实际的工具清单替换了旧版的通用描述，修正了 `bin/` 中存在的工具名称。
- 明确说明了 `client` 的执行方式与 `parse_output_metrics` 的解析约束。
- 为每个工具补充了输出示例与字段命名规则，方便前端解析与扩展。

## 下一步建议

- 为常用工具添加 `--help` 与 `--interval` 参数支持，并提供 `--json` 切换，便于 `client` 区分机器可读输出与人类可读日志。
- 在 `client/main.py` 中增加对 `PID_*`、`write_calls_pid_*` 等更丰富键名的解析测试用例。
- 编写短小的 `README` 片段并放置在每个工具源目录，说明用途与输出示例。

如果你希望我现在：
- 1) 将本次更新保存并提交为 patch（我可以生成 patch 并应用），或
- 2) 先把变更草案以 PR 描述格式输出供你审阅，
请回复你想要的下一步操作。 
