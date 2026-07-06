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

## 注意事项

- eBPF Exporter 需要较高权限，并且需要访问内核调试文件系统 `/sys/kernel/debug`。
- 如果没有看到指标，先检查容器是否正常启动，以及 Docker 是否有足够权限。
- 你可以根据需要在 `ebpf/` 目录下调整 eBPF 采集配置。
