# my_tools 文档

## 概要

`my_tools` 目录是一套基于 eBPF 的系统分析工具集合，用于采集 CPU、磁盘、文件、内存和网络相关的运行时指标。每个工具由 BPF 程序和用户态加载器组成，适合在 Linux 环境中执行，并可被 `client` 目录下的 Web 控制台统一管理。

## 目录结构

- `Makefile`：构建入口，支持按分类构建并生成 `bin/` 中的可执行文件。
- `common/`：公共头文件和类型定义，包含 `kernel_utils.h`、`vmlinux.h` 等。
- `bin/`：编译后输出的可执行工具，用于直接运行或被 `client` 调用。
- `cpu/`、`disk/`、`file/`、`memo/`、`network/`：按功能分类组织 BPF 程序和用户态加载器源码。
- `docs/README.md`：本说明文档。

## 设计理念

- 以 `libbpf` 为基础，将 BPF 程序与用户态逻辑分离。
- 通过 BPF map 聚合内核事件，用户态周期性读取并输出统计结果。
- 工具输出尽量保持简单、可解析，方便外部系统（例如 `client`）抓取和展示。
- 支持通过 `my_tools/bin` 目录作为统一挂载点，客户端能动态检测并执行工具。

## 构建流程

在 `my_tools` 根目录运行：

```bash
cd /home/takamiya/Document/gitee/libbpf-bootstrap/my_tools
make
```

可选构建分类：

```bash
make cpu
make disk
make file
make memo
make network
```

构建完成后，所有用户态工具会输出到：

- `my_tools/bin/`

执行示例：

```bash
sudo ./bin/irq_stat
sudo ./bin/tcp_connect
```

## 接口与挂载点

### 工具执行接口

每个工具通过二进制执行文件提供最简单的接口：

```bash
sudo ./bin/<tool_name>
```

该命令通常会持续运行，周期性输出当前统计信息；也可能在启动后打印单次结果并退出。工具对外输出遵循以下设计原则：

- 输出可直接阅读
- 优先支持 JSON 或 `key: value` 格式
- 方便外部解析器提取数字指标

### 统一挂载点

`my_tools/bin/` 是客户端管理的统一挂载点，`client/main.py` 会扫描该目录下所有可执行文件，并将其纳入可用工具列表。只要工具被放入此目录并具有执行权限，就能被 `client` 自动发现。

## 公共实现模式

### BPF 程序部分

- 文件名后缀为 `.bpf.c`
- 主要职责是：
  - 定义 eBPF maps
  - 附加 tracepoint 或 kprobe
  - 采集事件数据并写入 map
- 常见类型：
  - `BPF_MAP_TYPE_HASH`
  - `BPF_MAP_TYPE_ARRAY`
  - `BPF_MAP_TYPE_PERF_EVENT_ARRAY`

### 用户态加载器部分

- 文件名通常与目录名相同，例如 `cpu/irq_stat.c`
- 主要职责是：
  - 使用 `libbpf` 加载 BPF 程序
  - 附加 BPF 程序到内核事件
  - 循环读取 BPF map
  - 格式化输出统计结果

### 常见实现细节

- 使用 `vmlinux.h` 提取内核结构体定义，保证 BPF 程序与当前内核类型一致。
- 使用 `bpf_map__get_next_key()` 遍历 map，使用 `bpf_map__lookup_elem()` 读取数据。
- 通过 `signal(SIGINT)` 或轮询机制控制退出行为。
- 仅在必要时使用 kprobe/kretprobe 记录延迟，否则优先使用 tracepoint 简化稳定性。

## 工具分类与功能

### CPU 类

#### `irq_stat`

- 监控系统中断次数。
- 通过 `tracepoint/irq/irq_handler_entry` 记录每个 IRQ 的触发情况。
- 输出格式通常为 `IRQ: count` 列表。

### 磁盘类

#### `disk_delay`

- 统计磁盘写入延迟。
- 通过 kprobe 拦截关键写路径并计算时间差。
- 输出平均延迟、最大延迟等。

#### `disk_read_delay`

- 统计磁盘读取延迟。
- 跟踪读取入口和返回事件，聚合延迟数据。

### 文件类

#### `read_stat`

- 统计系统调用 `read()` 次数。
- 使用 tracepoint `sys_enter_read`。
- 输出 `read` 调用总次数和可能的按进程分布。

#### `write_stat`

- 统计系统调用 `write()` 次数。
- 使用 tracepoint `sys_enter_write`。

### 内存类

#### `mmap_stat`

- 统计 `mmap()` 调用与内存映射行为。
- 可用于分析内存映射热点和使用模式。

#### `oom_stat`

- 监控 OOM 事件发生。
- 适合用于发现内存压力引发的进程终止。

#### `page_fault_stat`

- 统计页面错误事件。
- 适用于分析缺页、页面访问模式。

#### `page_swap_stat`

- 统计页面交换行为。
- 监测 swap in/out 活动。

### 网络类

#### `tcp_connect`

- 统计 TCP 连接请求。
- 通过 kprobe 附加 `tcp_v4_connect` / `tcp_v6_connect`。
- 输出按 PID 或进程名统计的连接次数。

#### `socket_stat`

- 统计 `socket()` 调用的协议族分布。
- 使用 tracepoint `sys_enter_socket`。
- 输出协议族和创建次数。

## 工具接口设计建议

对于工具本身，建议保持以下接口风格：

- 默认为无参数运行
- 输出结构化文本或 JSON
- 支持通过 `SIGINT` 退出
- 提供周期性统计快照
- 可在用户态使用 `stderr` 输出诊断信息

## 与 client 的集成

`my_tools` 生成的工具可以直接被 `client` 前端调用。在集成时需要注意：

- `client` 默认使用 `sudo` 执行工具
- 工具应当放在 `my_tools/bin/`
- 输出应尽量可解析，以便 `client` 能提取数值指标
- 如果输出格式发生变化，应同步更新 `client/main.py` 中的 `parse_output_metrics()`

## 运行示例

```bash
cd /home/takamiya/Document/gitee/libbpf-bootstrap/my_tools
make network
sudo ./bin/tcp_connect
sudo ./bin/socket_stat
```

## 未来扩展

- 为 `bin/` 工具增加 CLI 参数支持，比如 `--interval`、`--pid`、`--json`
- 支持将输出写入 JSON/CSV 文件，方便后续分析
- 增加更多场景工具，如 XDP、cgroup skb、容器网络指标
- 将 `my_tools` 作为库组件，支持独立编程式调用
