# my_tools 文档

本目录提供 `my_tools` 工具集合的整体说明，包括目录结构、构建方式、每个工具接口使用与基本实现介绍。

## 目录结构

- `Makefile`：自定义构建规则，支持 `make`、`make cpu`、`make disk`、`make file`、`make memo`、`make network`。
- `common/vmlinux.h`：BPF 运行时和内核类型头文件，用于编译所有 BPF 程序。
- `bin/`：`make` 后生成的用户态可执行文件。
- `cpu/`：监控 CPU/系统事件的工具。
- `disk/`：监控磁盘和 I/O 延迟的工具。
- `file/`：统计文件系统读写调用的工具。
- `memo/`：监控内存和页面相关事件的工具。
- `network/`：监控网络相关事件的工具。

## 构建和运行

在 `my_tools` 目录下执行：

```bash
make
```

或仅构建某个分类：

```bash
make cpu
make disk
make file
make memo
make network
```

生成二进制文件位于 `bin/`，例如：

```bash
sudo ./bin/irq_stat
```

---

## 通用实现模式

每个工具通常包含两部分：

1. **BPF 程序**（`*.bpf.c`）：定义 BPF map、钩子点、事件处理逻辑。
2. **用户态程序**（`*.c`）：加载 BPF skeleton、附加 BPF 程序、读取 map 数据并打印统计结果。

常见实现细节：

- BPF 程序使用 `vmlinux.h`、`bpf_helpers.h`、`bpf_tracing.h`。
- 用户态程序使用 `libbpf` API，例如 `bpf_map__lookup_elem()`、`bpf_map__get_next_key()`、`bpf_map__update_elem()`。
- 使用 `signal(SIGINT)` 定义退出逻辑，或循环定时打印统计结果。

---

## 工具列表

### CPU 类

#### `irq_stat`

- 入口文件：`cpu/irq_stat.bpf.c`
- 用户态：`cpu/irq_stat.c`
- 作用：统计系统中断次数。
- 实现：
  - BPF 程序附加到 `tracepoint/irq/irq_handler_entry`。
  - 为每个 IRQ 编号维护一个 `BPF_MAP_TYPE_HASH`，记录 `count`。
  - 用户态每 2 秒遍历 map 并输出 `IRQ` 与 `Count`。

### 磁盘类

#### `disk_delay`

- 入口文件：`disk/disk_delay.bpf.c`
- 用户态：`disk/disk_delay.c`
- 作用：统计磁盘写操作从进入缓存到写盘的延迟。
- 实现：
  - 通过 kprobe 拦截 `vfs_write`, `ext4_file_write_iter`/`generic_file_write_iter`。
  - 利用 `ts_start_pid` 记录起始时间，`stats` map 聚合延迟数据。
  - 用户态每秒打印平均写入延迟和缓存刷新延迟。

#### `disk_read_delay`

- 入口文件：`disk/disk_read_delay.bpf.c`
- 用户态：`disk/disk_read_delay.c`
- 作用：统计磁盘读取延迟。
- 实现：
  - 拦截 `vfs_read` 的入口和返回。
  - 记录开始时间并在返回时更新延迟统计 map。

### 文件类

#### `read_stat`

- 入口文件：`file/read_stat.bpf.c`
- 用户态：`file/read_stat.c`
- 作用：统计系统调用 `read()` 的调用次数。
- 实现：
  - 使用 tracepoint `sys_enter_read`。
  - 在 BPF map 中记录调用计数。
  - 用户态定期查询 map 并打印当前 `read` 调用次数。

#### `write_stat`

- 入口文件：`file/write_stat.bpf.c`
- 用户态：`file/write_stat.c`
- 作用：统计系统调用 `write()` 的调用次数。
- 实现：
  - 使用 tracepoint `sys_enter_write`。
  - 在 BPF map 中维护计数。
  - 用户态定时读取并输出写调用总数。

### 内存类

#### `mmap_stat`

- 入口文件：`memo/mmap_stat.bpf.c`
- 用户态：`memo/mmap_stat.c`
- 作用：统计进程 `mmap` 调用次数或内存映射行为。

#### `oom_stat`

- 入口文件：`memo/oom_stat.bpf.c`
- 用户态：`memo/oom_stat.c`
- 作用：监控 OOM 事件。

#### `page_fault_stat`

- 入口文件：`memo/page_fault_stat.bpf.c`
- 用户态：`memo/page_fault_stat.c`
- 作用：统计页面错误。

#### `page_swap_stat`

- 入口文件：`memo/page_swap_stat.bpf.c`
- 用户态：`memo/page_swap_stat.c`
- 作用：统计页面交换行为。

### 网络类

#### `tcp_connect`

- 入口文件：`network/tcp_connect.bpf.c`
- 用户态：`network/tcp_connect.c`
- 作用：统计 TCP `connect` 调用次数。
- 接口：
  - `sudo ./bin/tcp_connect`
- 实现：
  - 使用 kprobe 附加到 `tcp_v4_connect` 和 `tcp_v6_connect`。
  - 使用 `BPF_MAP_TYPE_HASH` 按 PID 记录连接次数和进程名。
  - 用户态每 2 秒遍历 map 并输出 `PID`, `COMM`, `COUNT`。

#### `socket_stat`

- 入口文件：`network/socket_stat.bpf.c`
- 用户态：`network/socket_stat.c`
- 作用：统计 `socket()` 创建的协议族分布。
- 接口：
  - `sudo ./bin/socket_stat`
- 实现：
  - 使用 tracepoint `sys_enter_socket`。
  - 使用 `BPF_MAP_TYPE_HASH` 按协议族统计创建次数。
  - 用户态定期遍历 map 并输出 `FAMILY`, `COUNT`。

---

## 开发要点

1. **BPF map 类型选择**
   - 计数聚合通常使用 `BPF_MAP_TYPE_HASH` 或 `BPF_MAP_TYPE_ARRAY`。
   - 需要通过 `bpf_map_lookup_elem()`/`bpf_map_update_elem()` 更新值。

2. **程序附加方式**
   - `tracepoint/`：推荐用于系统调用和内核事件。
   - `kprobe` / `kretprobe`：用于函数入口/返回时间测量。

3. **用户态打印方式**
   - 通过 `bpf_map__get_next_key()` 遍历 map。
   - 通过 `bpf_map__lookup_elem()` 读取 value。

4. **构建依赖**
   - 依赖内置 `libbpf` 的头文件和静态库。
   - 通过 `bpftool gen skeleton` 生成 skeleton 头文件。

## 参考命令

```bash
cd my_tools
make network
sudo ./bin/tcp_connect
sudo ./bin/socket_stat
```

---

## 未来扩展建议

- 添加更多网络工具，例如 `xdp` 或 `cgroup_skb` 分析。
- 将结果导出为 CSV 或 JSON，便于后续分析。
- 为每个工具增加命令行参数支持，例如采样间隔、过滤 PID、输出排序等。
