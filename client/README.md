# my_tools Prometheus + Grafana Client

## 概要

`client` 目录下的客户端是一个基于 FastAPI 的 Web 应用，旨在简化 `my_tools` 目录下 BPF 工具的运行、监控和可视化。它提供：

- 可点击的 Web 控制台，触发 `my_tools/bin` 中的工具
- Prometheus 指标导出 `/metrics`
- 运行状态、历史统计和输出日志展示
- 与 Grafana 无缝对接的可视化方案

## 实现技术

- 语言：Python 3.12+
- Web 框架：FastAPI
- 模板引擎：Jinja2
- HTTP 服务器：uvicorn
- Prometheus 客户端：prometheus-client
- 前端：HTML + CSS + Chart.js

## 目录与关键文件

- `main.py`：应用入口，包含 API 定义、工具执行逻辑、Prometheus 指标和内存统计缓存。
- `pyproject.toml`：Python 包和依赖配置。
- `templates/index.html`：前端页面模板。
- `static/`：前端样式和 JS 脚本。
- `../my_tools/bin/`：可执行工具目录，客户端从这里动态发现可用工具。

## 核心架构

### 后端

`main.py` 负责：

- 自动扫描 `my_tools/bin` 下可执行文件，构建 `ALLOWED_TOOLS`
- 提供 REST API：
  - `GET /`：前端页面
  - `GET /metrics`：Prometheus 指标
  - `GET /api/tools`：可用工具列表
  - `GET /api/stats`：当前统计数据快照
  - `POST /run/{tool_name}`：触发工具执行
- 启动子进程运行指定工具，捕获 stdout/stderr，提取数值指标并更新 Prometheus metric
- 保护执行文件路径，避免任意命令注入

### 前端

前端模板使用 `templates/index.html` 和静态资源，提供：

- 工具选择列表
- 运行时长配置
- 实时输出日志展示
- 运行统计面板
- 指标可视化图表占位

### Prometheus 集成

客户端在 `/metrics` 暴露指标，支持直接被 Prometheus 抓取。当前指标包括：

- `my_tools_tool_run_count{tool, status}`
- `my_tools_tool_last_duration_seconds{tool}`
- `my_tools_tool_last_success{tool}`
- `my_tools_tool_last_output_bytes{tool}`
- `my_tools_tool_running`

## 接口设计

### 1. `GET /`

返回前端页面，包含工具选择、运行控制、图表和日志面板。

### 2. `GET /metrics`

返回 Prometheus 格式指标，用于监控系统和 Grafana 数据源。

### 3. `GET /api/tools`

返回可用工具列表 JSON：

```json
{
  "tools": [
    {"name": "irq_stat", "icon": "⚡", "summary": "Irq stat"},
    ...
  ]
}
```

### 4. `GET /api/stats`

返回当前统计快照，包括运行次数、状态分布、最近一次输出大小、运行时长等。

### 5. `GET /run/stream/{tool_name}`

以 SSE（Server-Sent Events）方式触发工具运行并流式返回输出。

参数：

- `tool_name`：可执行工具名称，必须存在于 `my_tools/bin`
- `duration`：可选查询参数，超时时间，默认 `10.0` 秒

返回值：

- 流式事件 `data`，逐行返回工具输出
- 结束事件包含最终状态、运行时长、解析后的指标、摘要和日志路径

客户端示例：

```js
const es = new EventSource(`/run/stream/${tool}?duration=${duration}`);
es.onmessage = (ev) => {
  const data = JSON.parse(ev.data);
  if (data.line) {
    // 追加日志行
  }
  if (data.status) {
    // 读取最终结果
  }
};
```

### 运行逻辑

- 使用 `subprocess.Popen` 启动命令
- 以 `sudo` 执行工具：`sudo <tool_path>`
- 限制执行时间为 `duration`
- 捕获 stdout/stderr，组合为最终输出
- 解析输出中的数值指标，支持：
  - JSON 对象
  - `key: value` 或 `key = value` 格式
  - 文本中的键值对
- 更新内存统计缓存 `TOOL_STATS`
- 更新 Prometheus 指标

## 挂载点说明

- `/`：Web 控制台主页面
- `/static/`：静态文件资源挂载点
- `/metrics`：Prometheus 指标挂载点
- `/api/tools`：工具列表接口
- `/api/stats`：运行统计接口
- `/run/{tool_name}`：工具执行接口

## 运行方式

```bash
cd /home/takamiya/Document/gitee/libbpf-bootstrap/client
uv run main.py
```

打开浏览器访问：

```text
http://127.0.0.1:8000/
```

## Prometheus 配置示例

```yaml
scrape_configs:
  - job_name: my_tools_client
    static_configs:
      - targets: ["127.0.0.1:8000"]
```

## Grafana 可视化建议

- `my_tools_tool_last_duration_seconds`：运行时长趋势
- `my_tools_tool_run_count`：工具调用次数
- `my_tools_tool_last_success`：成功/失败状态
- `my_tools_tool_last_output_bytes`：输出大小变化

## 扩展建议

- 为接口增加用户身份验证
- 支持不使用 `sudo` 的工具执行方式
- 支持按工具参数传递过滤器、PID 等
- 将前端图表扩展为实时分布和历史面板
- 增加 `/api/metrics/{tool_name}` 细粒度查询接口
