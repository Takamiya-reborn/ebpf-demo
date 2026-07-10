## 项目整体架构与设计思想

这个仓库不仅是 libbpf 的示例集合，也是一个面向系统观测与性能分析的分层式 eBPF 工程。它把“内核态数据采集、用户态数据处理、数据展示”拆分为三个相对独立的层次，从而实现更清晰的职责边界与更灵活的扩展能力。

```mermaid
flowchart TB
    subgraph Kernel[内核态：eBPF 数据采集层]
        A[tracepoint / kprobe / uprobe 等挂载点]
        B[eBPF 程序]
        C[BPF map / ring buffer / perf event]
    end

    subgraph User[用户态：数据处理与控制层]
        D[my_tools 中的采集工具]
        E[FastAPI Web 客户端]
        F[指标解析、聚合与状态管理]
        G[/metrics Prometheus 指标暴露]
    end

    subgraph Display[展示层：可视化与监控层]
        H[Prometheus]
        I[Grafana]
        J[Web 控制台]
    end

    A --> B --> C --> D
    D --> E --> F --> G
    G --> H --> I
    E --> J
```

### 1. 内核态数据采集层

这一层主要由 [my_tools](my_tools) 目录下的 eBPF 程序构成。每个采集工具都基于 libbpf 加载 BPF 程序，并将其挂载到内核中的关键事件点，例如 CPU、磁盘、文件、内存与网络相关的 tracepoint 或 kprobe。采集逻辑通常包括：

- 在内核中执行轻量级事件过滤与统计；
- 使用 BPF map 保存中间状态，例如计数、延迟和分布信息；
- 通过 ring buffer 或 perf event 将结果回传给用户态；
- 生成易于消费的文本或结构化指标输出。

这层的核心目标是尽量把计算下沉到内核，从而降低数据采集对系统性能的影响。

### 2. 用户态数据处理与控制层

这一层由 [client](client) 目录中的 FastAPI 服务承担。它负责对采集结果进行二次处理，并为使用者提供统一的控制入口。具体职责包括：

- 扫描 [my_tools/bin](my_tools/bin) 下的可执行采集工具，动态生成可调用工具列表；
- 通过子进程调用对应工具，捕获 stdout/stderr 输出；
- 对输出进行解析、聚合和摘要计算，形成更适合展示的指标；
- 暴露 `/metrics` 接口，给 Prometheus 拉取使用；
- 提供 Web 控制台和流式输出能力，便于实时观察采集结果。

这里的设计思路是把“采集”和“展示”解耦，用户态服务仅负责处理与编排，而不直接参与内核级事件的采集逻辑。

### 3. 数据展示与监控层

这一层由 [prometheus-demo](prometheus-demo) 目录中的 Prometheus、Grafana、Node Exporter 和 eBPF Exporter 组成。它们负责把用户态生成的指标进行长期存储、查询和可视化展示：

- Prometheus 负责抓取 `/metrics` 并存储时间序列数据；
- Grafana 提供图表、面板和仪表盘能力；
- Node Exporter 与 eBPF Exporter 提供基础系统和内核级观测数据；
- Web 控制台则用于即时触发工具和查看原始输出。

这样就形成了从“实时采样”到“长期观察”的完整链路。

### 业务流程：eBPF 数据采集 - 数据处理 - 数据展示

整个系统的业务流程可以概括为以下几步：

1. 用户在 Web 控制台或接口中选择需要执行的采集任务；
2. 客户端调用对应的 eBPF 工具，工具在内核中挂载探针并开始采集事件；
3. eBPF 程序将统计结果通过 BPF map、ring buffer 或 perf event 传递给用户态；
4. 用户态服务读取输出、解析指标、生成摘要并更新 Prometheus 指标；
5. Prometheus 定时抓取这些指标，Grafana 根据时间序列进行可视化展示。

### 数据通信方式与交互机制

- 内核态到用户态：通过 BPF map、ring buffer 和 perf event 完成高效的数据回传；
- 用户态内部：通过 FastAPI 的 HTTP API、SSE 流式输出以及子进程调用完成控制与结果传递；
- 用户态到监控系统：通过 Prometheus 的拉取机制读取 `/metrics`；
- 展示层交互：Grafana 从 Prometheus 查询数据并生成图表，Web 页面则直接展示运行状态和日志。

这种三层架构使系统具备较强的可维护性：内核层专注采集，用户态层负责处理与控制，展示层负责可视化与分析，三者通过清晰的接口边界协同工作。