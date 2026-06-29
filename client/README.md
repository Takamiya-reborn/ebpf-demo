# my_tools Prometheus + Grafana Client

这是一个用于 `my_tools` 目录下工具的可视化客户端。

## 功能

- 提供 Web 控制台，可点击图标执行工具
- 将工具运行结果与执行状态导出到 Prometheus `/metrics`
- 支持 Grafana 通过 Prometheus 数据源进行可视化

## 运行方式

使用 uv 命令运行项目：

```bash
cd /home/takamiya/Document/gitee/libbpf-bootstrap/client
python3 -m uv run python main.py
```

然后打开浏览器访问 `http://127.0.0.1:8000/`

## Prometheus 配置

添加以下 scrape 配置：

```yaml
scrape_configs:
  - job_name: my_tools_client
    static_configs:
      - targets: ["127.0.0.1:8000"]
```

## Grafana 可视化建议

- 使用 `my_tools_tool_last_duration_seconds` 展示运行时长折线图
- 使用 `my_tools_tool_run_count` 展示每个工具执行次数饼图或柱状图
- 使用 `my_tools_tool_last_success` 展示最近成功/失败状态
- 使用 `my_tools_tool_last_output_bytes` 展示最近输出大小
