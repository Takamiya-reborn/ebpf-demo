# Prometheus Demo

这个目录提供了一个基于 Docker 的本地监控示例，使用 Prometheus、Grafana、Node Exporter 和 eBPF Exporter 来采集系统与内核级指标。

## 组件说明

- Prometheus：负责拉取并存储监控指标
- Grafana：提供可视化界面
- Node Exporter：采集主机的 CPU、内存、磁盘、网络等基础指标
- eBPF Exporter：通过 eBPF 从内核中采集更细粒度的性能指标

## 目录结构

- docker-compose.yml：定义所有容器服务
- prometheus.yml：Prometheus 的抓取配置
- ebpf/：eBPF exporter 的配置目录

## 快速开始

1. 确保机器上已安装 Docker 和 Docker Compose 插件。
2. 进入当前目录：

```bash
cd /path/to/prometheus-demo
```

3. 启动所有服务：

```bash
docker compose up -d
```

4. 访问以下地址：

- Prometheus: http://localhost:9091
- Grafana: http://localhost:3000
- Node Exporter: http://localhost:9100/metrics
- eBPF Exporter: http://localhost:9435/metrics

所有端口都只绑定在 `127.0.0.1` 上，仅本机可访问；如需从外部访问，请修改 `docker-compose.yml` 中对应的端口映射。

## 常用命令

查看服务状态：

```bash
docker compose ps
```

查看日志：

```bash
docker compose logs -f
```

停止并删除容器：

```bash
docker compose down
```

停止并删除容器及数据卷（会清空 Prometheus/Grafana 的持久化数据）：

```bash
docker compose down -v
```

## 注意事项

- eBPF Exporter 以 `cap_add: BPF/PERFMON/SYS_RESOURCE` 能力运行（未使用 `privileged`），并且需要访问内核调试文件系统 `/sys/kernel/debug`；`ebpf/` 配置目录以只读方式挂载。
- `prometheus.yml` 中的抓取目标使用 Compose 服务名（如 `node-exporter:9100`、`ebpf-exporter:9435`），请勿改回 `localhost`。
- 各服务均配置了健康检查，可通过 `docker compose ps` 查看健康状态；Prometheus 数据保留 15 天（`--storage.tsdb.retention.time=15d`）。
- 如果没有看到指标，先检查容器是否正常启动，以及 Docker 是否有足够权限。
- 你可以根据需要在 `ebpf/` 目录下调整 eBPF 采集配置（启用新配置需同步修改 `docker-compose.yml` 中的 `--config.names`）。
- `ebpf/` 下的 `.bpf.o` 是预编译的 eBPF 目标文件，源自 `../my_tools/` 的源码构建，本目录没有自动同步机制——修改 `my_tools` 源码后需要手动执行 `make` 并将 `.output/<tool>.bpf.o` 复制到 `ebpf/<category>/` 下对应位置。kprobe 类工具（如 `disk_read_delay`）对内核版本有要求，内核不匹配时可能加载失败。
- 默认只启用了 10 个采集配置（见 `docker-compose.yml` 中 `--config.names`），`ebpf/` 目录下其余配置未启用。
- Grafana 默认账号密码为 `admin/admin`（可通过环境变量 `GRAFANA_ADMIN_PASSWORD` 覆盖），首次登录后请立即修改密码。Grafana 与 Prometheus 的数据已通过 named volume 持久化，`docker compose down` 不会丢失数据（`down -v` 会）。
