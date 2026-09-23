# bpf-demo

基于 [libbpf-bootstrap](https://github.com/libbpf/libbpf-bootstrap) 的 eBPF 学习与性能观测项目。仓库包含上游示例、面向 CPU/磁盘/文件/内存/网络的自定义 eBPF 工具，以及一个可选的 Web 控制台和 Prometheus 监控环境。

## 项目组成

- `examples/`：libbpf-bootstrap 自带的 C/Rust 示例，用于学习 CO-RE、tracepoint、kprobe、fentry、uprobe、USDT、XDP 等用法。
- `my_tools/`：自定义性能观测工具。每个工具通常由用户态程序 `*.c` 和 BPF 程序 `*.bpf.c` 组成。
  - `cpu/`：CPU 使用率、频率、中断和运行队列延迟
  - `disk/`：磁盘 I/O 大小、调度及读写延迟
  - `file/`：文件打开、读写和 `fsync` 统计
  - `memo/`：内存映射、缺页、换页和 OOM 统计
  - `network/`：Socket、TCP 连接、UDP 流量和 TCP 重传统计
- `client/`：FastAPI Web 控制台。动态发现 `my_tools/bin/` 中的可执行文件，通过 SSE 实时显示工具输出，并对数值结果做摘要和图表展示；工具的图表和分类配置维护在 `client/config/tool_configs.json`。
- `prometheus-demo/`：Docker Compose 监控环境，组合 Prometheus、Grafana、Node Exporter 和 Cloudflare eBPF Exporter。
- `run_logs/`：本地运行产生的历史日志目录，不属于源码，已加入 Git 忽略规则。

## 环境要求

建议在 Linux 主机上运行 eBPF 工具。需要准备：

- 支持目标 eBPF 特性的 Linux 内核，并能访问 `/sys/kernel/debug`；
- `clang`、`llvm`、`bpftool`、`gcc`、`make`；
- `libelf`、`zlib`、`libbpf` 的构建依赖；
- 运行工具通常需要 root 权限，或配置 `sudo -n` 免交互执行；
- 使用 Web 控制台需要 Python 3.12+ 和 `uv`；
- 使用监控演示需要 Docker Compose。

Windows/WSL 仅适合作为开发环境；真正加载 eBPF 程序时，应确认 Linux 内核、权限和调试文件系统可用。

## 构建自定义工具

在仓库根目录执行：

```bash
cd my_tools
make
```

构建产物位于 `my_tools/bin/`，中间文件位于 `my_tools/.output/`。按类别构建或清理：

```bash
make cpu
make network
make clean
```

运行单个工具时使用生成的可执行文件，例如：

```bash
sudo ./my_tools/bin/cpu_usage
sudo ./my_tools/bin/tcp_connect
```

具体输出格式和事件字段以对应目录中的 `*.c`、`*.bpf.c` 实现为准。

## 启动 Web 控制台

先完成 `my_tools` 构建，再安装客户端依赖并启动服务：

```bash
cd client
uv sync
uv run main.py(默认监听所有端口，访问本机使用localhost:8000)
```

访问 <http://127.0.0.1:8000/>。控制台会扫描 `my_tools/bin/`，通过 `sudo -n` 启动选中的工具，默认采集 10 秒，并在页面中实时显示 stdout/stderr。

客户端还提供：

- `GET /api/tools`：列出可用工具；
- `GET /run/stream/{tool_name}?duration=10`：以 SSE 流式执行工具；
- 当前版本的 Prometheus 指标主要由 `prometheus-demo` 中的 Prometheus 和 eBPF Exporter 采集；客户端本身暂未注册 `/metrics` 路由。

工具输出会保存在内存中用于当前请求的解析和摘要，不会由当前客户端自动写入 `run_logs/`。

## 启动 Prometheus 监控环境

```bash
cd prometheus-demo
docker compose up -d
docker compose ps
```

默认地址：

- Prometheus：<http://127.0.0.1:9091>
- Grafana：<http://127.0.0.1:3000>
- Node Exporter：<http://127.0.0.1:9100/metrics>
- eBPF Exporter：<http://127.0.0.1:9435/metrics>

停止服务：

```bash
docker compose down
```

`prometheus-demo/ebpf/` 中的 YAML 是 eBPF Exporter 配置；启用哪些配置由 `docker-compose.yml` 中 `--config.names` 决定。eBPF Exporter 使用特权容器并挂载 `/sys/kernel/debug`，请只在可信的本地环境中使用。

## 日志与 Git

`run_logs/` 是运行结果，不是可复现的源码或测试夹具。它可能包含大量、与机器环境相关的文件，因此不应提交到 Git。当前仓库已通过 `.gitignore` 忽略该目录；客户端也不再创建未使用的持久化日志目录。网页中的“实时输出日志”仍然是请求期间的内存数据，不等同于文件日志。

如需临时保存一次运行结果，建议显式重定向到仓库外或本地临时目录：

```bash
sudo ./my_tools/bin/disk_read_delay > /tmp/disk_read_delay.log 2>&1
```

## 相关文档

- [上游 libbpf-bootstrap 示例说明](README.md)
- [Web 客户端说明](client/README.md)
- [Prometheus Demo 说明](prometheus-demo/README.md)
