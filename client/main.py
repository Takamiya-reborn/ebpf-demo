import os
import subprocess
import time
import json
import re
from pathlib import Path
from typing import Dict

from fastapi import FastAPI, HTTPException, Request, Query
from fastapi.responses import HTMLResponse, JSONResponse, EventSourceResponse
import uuid
from fastapi.staticfiles import StaticFiles
from jinja2 import Environment, FileSystemLoader
from prometheus_client import (
    CollectorRegistry,
    Counter,
    Gauge,
    CONTENT_TYPE_LATEST,
    generate_latest,
)

PROJECT_ROOT = Path(__file__).resolve().parent.parent
TOOL_BIN_DIR = PROJECT_ROOT / "my_tools" / "bin"
RUN_LOG_DIR = PROJECT_ROOT / "run_logs"
RUN_LOG_DIR.mkdir(parents=True, exist_ok=True)

STATUS_ICON = {"success": "✅", "error": "❌", "timeout": "⏱️", "unknown": "⚠️"}

TOOL_CONFIGS: Dict[str, Dict] = {
    "disk_write_delay": {
        "category": "latency",
        "chart_type": "bar",
        "x_axis": "操作阶段",
        "y_axis": "延迟时间",
        "unit": "us",
        "can_average": True,
    },
    "disk_read_delay": {
        "category": "latency",
        "chart_type": "bar",
        "x_axis": "操作/延迟区间",
        "y_axis": "延迟时间",
        "unit": "us",
        "can_average": True,
    },
    "irq_stat": {
        "category": "irq",
        "chart_type": "bar",
        "x_axis": "IRQ 向量号",
        "y_axis": "中断次数",
        "unit": "次",
        "can_average": False,  # IRQ编号求平均值无物理意义
    },
    "socket_stat": {
        "category": "socket",
        "chart_type": "pie",   # 占比类数据，前端自动画饼图
        "x_axis": "Socket 协议族",
        "y_axis": "连接数",
        "unit": "个",
        "can_average": False,
    },
    "tcp_connect": {
        "category": "tcp_connect",
        "chart_type": "bar",
        "x_axis": "连接端点",
        "y_axis": "请求数",
        "unit": "次",
        "can_average": False,
    },
    # 所有的 PID 计数器类工具
    "read_stat": {"category": "counter_by_pid", "chart_type": "bar", "x_axis": "PID", "y_axis": "读系统调用次数", "unit": "次", "can_average": False},
    "write_stat": {"category": "counter_by_pid", "chart_type": "bar", "x_axis": "PID", "y_axis": "写系统调用次数", "unit": "次", "can_average": False},
    "mmap_stat": {"category": "counter_by_pid", "chart_type": "bar", "x_axis": "PID", "y_axis": "mmap次数", "unit": "次", "can_average": False},
    "oom_stat": {"category": "counter_by_pid", "chart_type": "bar", "x_axis": "PID", "y_axis": "OOM Kill 次数", "unit": "次", "can_average": False},
    "page_fault_stat": {"category": "counter_by_pid", "chart_type": "bar", "x_axis": "PID", "y_axis": "缺页中断次数", "unit": "次", "can_average": False},
    "page_swap_stat": {"category": "counter_by_pid", "chart_type": "bar", "x_axis": "PID", "y_axis": "页面交换次数", "unit": "次", "can_average": False},
}

# 默认降级配置（未显式配置的工具自动套用此规则）
DEFAULT_CONFIG = {
    "category": "generic",
    "chart_type": "bar",
    "x_axis": "指标名称",
    "y_axis": "数值",
    "unit": "",
    "can_average": True,
}


def get_available_tools():
    return sorted(
        [
            tool.name
            for tool in TOOL_BIN_DIR.iterdir()
            if tool.is_file() and os.access(tool, os.X_OK)
        ]
    )


ALLOWED_TOOLS = get_available_tools()

app = FastAPI(
    title="my_tools Prometheus + Grafana UI",
    description="Expose my_tools metrics to Prometheus and provide a clickable web dashboard.",
    version="0.1.0",
)
app.mount("/static", StaticFiles(directory=Path(__file__).resolve().parent / "static"), name="static")

jinja_env = Environment(
    loader=FileSystemLoader(Path(__file__).resolve().parent / "templates"),
    autoescape=True,
)

registry = CollectorRegistry()

tool_run_count = Counter(
    "my_tools_tool_run_count",
    "Total execution count per my_tools command",
    ["tool", "status"],
    registry=registry,
)
tool_last_duration = Gauge(
    "my_tools_tool_last_duration_seconds",
    "Duration of the most recent my_tools command run",
    ["tool"],
    registry=registry,
)
tool_last_success = Gauge(
    "my_tools_tool_last_success",
    "Whether the most recent my_tools command succeeded (1) or failed (0)",
    ["tool"],
    registry=registry,
)
tool_last_output_bytes = Gauge(
    "my_tools_tool_last_output_bytes",
    "Size in bytes of the most recent my_tools command output",
    ["tool"],
    registry=registry,
)
tool_running = Gauge(
    "my_tools_tool_running",
    "Number of my_tools commands currently running",
    registry=registry,
)

TOOL_STATS: Dict[str, Dict] = {}
for t in ALLOWED_TOOLS:
    TOOL_STATS[t] = {
        "run_count": 0,
        "run_count_by_status": {},
        "last_duration": 0.0,
        "last_output_bytes": 0,
        "last_success": 0,
        "running": 0,
    }


def make_tool_info(name: str) -> Dict:
    config = TOOL_CONFIGS.get(name, DEFAULT_CONFIG)
    return {
        "name": name,
        "summary": name.replace("_", " ").capitalize(),
        "config": config,  # 将配置同步传给前端
    }

def parse_output_metrics(output: str) -> Dict[str, float]:
    out = {}
    if not output:
        return out

    # 预先定义正则，提高性能
    kv_re = re.compile(r"^\s*([A-Za-z0-9_.-]+)\s*(?:[:=])\s*([-+]?\d*\.?\d+(?:[eE][-+]?\d+)?)\s*$")

    for ln in output.splitlines():
        m = kv_re.match(ln)
        if m:
            key = m.group(1)
            val = float(m.group(2))
            # ⭐ 核心改动：不再是直接赋值，而是累加
            out[key] = out.get(key, 0.0) + val
            continue # 匹配成功就跳过，进入下一行

        try:
            j = json.loads(ln)
            if isinstance(j, dict):
                for k, v in j.items():
                    if isinstance(v, (int, float)):
                        out[str(k)] = out.get(str(k), 0.0) + float(v)
        except:
            pass
            
    return out

def get_tool_category(tool_name: str) -> str:
    return TOOL_CONFIGS.get(tool_name, DEFAULT_CONFIG)["category"]

def summarize_tool_metrics(tool_name: str, metrics: Dict[str, float], duration_seconds: float) -> Dict:
    config = TOOL_CONFIGS.get(tool_name, DEFAULT_CONFIG)
    category = config["category"]

    # 1. 数据分类：指标 vs 计数
    metric_entries = {k: v for k, v in metrics.items() if not k.lower().endswith(('_count', '_cnt'))}
    counter_entries = {k: v for k, v in metrics.items() if k.lower().endswith(('_count', '_cnt'))}

    metric_values = list(metric_entries.values())
    total_samples = sum(counter_entries.values()) if counter_entries else float(len(metric_values))
    total_metric_sum = sum(metric_values) if metric_values else 0.0

    # 2. 基础统计结构
    summary = {
        "tool_type": category,
        "count": total_samples,
        "sum": total_metric_sum,
        "min": float(min(metric_values)) if metric_values else 0.0,
        "max": float(max(metric_values)) if metric_values else 0.0,
        "avg": (total_metric_sum / total_samples) if total_samples > 0 else 0.0,
        "rate_per_sec": total_samples / duration_seconds if duration_seconds > 0 else 0.0,
        "top_keys": [
            {"key": k, "value": v, "percent": round((v / total_metric_sum * 100), 2) if total_metric_sum else 0.0}
            for k, v in sorted(metric_entries.items(), key=lambda x: x[1], reverse=True)[:5]
        ]
    }

    # 3. 针对不同类型的特殊补充
    if not config.get("can_average", True):
        summary["avg"] = None

    if category == "latency":
        derived = []
        for key, value in metric_entries.items():
            # 自动寻找匹配的 count 键进行细分计算
            base_name = re.sub(r'(_total_us|_total_ms|_total_latency_ns|_total)$', '', key)
            cnt = metrics.get(f"{base_name}_count") or metrics.get(f"{base_name}_cnt", 0)
            unit = "ms" if "ms" in key else ("ns" if "ns" in key else "us")
            if cnt > 0:
                derived.append({"name": base_name, "total": value, "count": cnt, "avg": value/cnt, "unit": unit})
        summary["latency_groups"] = derived

    return summary

@app.get("/", response_class=HTMLResponse)
async def index(request: Request) -> HTMLResponse:
    tools = [make_tool_info(name) for name in ALLOWED_TOOLS]
    template = jinja_env.get_template("index.html")
    html = template.render(
        request=request,
        tools=tools,
        metrics_path="/metrics",
        info="点击图标即可执行工具，并将输出与运行指标导出为 Prometheus 指标供 Grafana 使用。",
    )
    return HTMLResponse(content=html)


@app.get("/metrics")
async def metrics() -> HTMLResponse:
    data = generate_latest(registry)
    return HTMLResponse(content=data, media_type=CONTENT_TYPE_LATEST)


@app.get("/api/tools")
async def api_tools() -> JSONResponse:
    tools = [make_tool_info(name) for name in get_available_tools()]
    return JSONResponse({"tools": tools})


@app.get("/api/stats")
async def api_stats() -> JSONResponse:
    return JSONResponse({"stats": TOOL_STATS, "running": int(tool_running._value.get())})


@app.post("/run/{tool_name}")
async def run_tool(tool_name: str, duration: float = Query(10.0)) -> JSONResponse:
    available_tools = get_available_tools()
    if tool_name not in available_tools:
        raise HTTPException(status_code=404, detail=f"Unknown tool: {tool_name}")

    tool_path = TOOL_BIN_DIR / tool_name
    if not tool_path.exists() or not os.access(tool_path, os.X_OK):
        raise HTTPException(status_code=404, detail=f"Tool binary not found or not executable: {tool_name}")

    tool_running.inc()
    TOOL_STATS.setdefault(tool_name, {})
    TOOL_STATS[tool_name]["running"] = 1
    start = time.time()
    output = ""
    status = "unknown"
    parsed = {}
    parsed_summary = {}

    try:
        cmd = ["sudo", str(tool_path)]
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            out, err = proc.communicate(timeout=duration)
            output = (out or "") + (err or "")
            duration_actual = time.time() - start
            status = "success" if proc.returncode == 0 else "error"
        except subprocess.TimeoutExpired:
            try:
                proc.terminate()
                out, err = proc.communicate(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                out, err = proc.communicate()
            output = (out or "") + (err or "")
            duration_actual = time.time() - start
            status = "timeout"

        parsed = parse_output_metrics(output)
        parsed_summary = summarize_tool_metrics(tool_name, parsed, duration_actual) if parsed else {}

        tool_run_count.labels(tool=tool_name, status=status).inc()
        tool_last_success.labels(tool=tool_name).set(1 if status == "success" else 0)

        TOOL_STATS[tool_name]["run_count"] = TOOL_STATS[tool_name].get("run_count", 0) + 1
        rcbs = TOOL_STATS[tool_name].setdefault("run_count_by_status", {})
        rcbs[status] = rcbs.get(status, 0) + 1
        TOOL_STATS[tool_name]["last_duration"] = duration_actual
        TOOL_STATS[tool_name]["last_output_bytes"] = len(output.encode("utf-8", "replace"))
        TOOL_STATS[tool_name]["last_success"] = 1 if status == "success" else 0
        TOOL_STATS[tool_name]["last_parsed"] = parsed
        TOOL_STATS[tool_name]["last_parsed_summary"] = parsed_summary

    except Exception as exc:
        duration_actual = time.time() - start
        status = "error"
        output = f"Execution failed: {exc}"
        tool_run_count.labels(tool=tool_name, status=status).inc()
        tool_last_success.labels(tool=tool_name).set(0)
        TOOL_STATS[tool_name]["run_count"] = TOOL_STATS[tool_name].get("run_count", 0) + 1
        rcbs = TOOL_STATS[tool_name].setdefault("run_count_by_status", {})
        rcbs[status] = rcbs.get(status, 0) + 1
        TOOL_STATS[tool_name]["last_duration"] = duration_actual
        TOOL_STATS[tool_name]["last_output_bytes"] = len(output.encode("utf-8", "replace"))
        TOOL_STATS[tool_name]["last_success"] = 0
    finally:
        tool_running.dec()
        TOOL_STATS[tool_name]["running"] = 0
    tool_last_duration.labels(tool=tool_name).set(duration_actual)
    tool_last_output_bytes.labels(tool=tool_name).set(len(output.encode("utf-8", "replace")))

    # 将该工具的元数据返回给前端
    config = TOOL_CONFIGS.get(tool_name, DEFAULT_CONFIG)

    return JSONResponse(
        {
            "tool": tool_name,
            "status": status,
            "icon": STATUS_ICON.get(status, STATUS_ICON["unknown"]),
            "duration_seconds": round(duration_actual, 3),
            "output": output,
            "config": config,  # ⭐ 新增：向前端输出工具特有的坐标轴定义等信息
            "parsed_metrics": TOOL_STATS[tool_name].get("last_parsed", {}),
            "parsed_summary": TOOL_STATS[tool_name].get("last_parsed_summary", {}),
        }
    )


@app.get("/run/stream/{tool_name}")
async def run_tool_stream(tool_name: str, duration: float = Query(10.0)):
    available_tools = get_available_tools()
    if tool_name not in available_tools:
        raise HTTPException(status_code=404, detail=f"Unknown tool: {tool_name}")

    tool_path = TOOL_BIN_DIR / tool_name
    if not tool_path.exists() or not os.access(tool_path, os.X_OK):
        raise HTTPException(status_code=404, detail=f"Tool binary not found or not executable: {tool_name}")

    run_id = uuid.uuid4().hex
    tool_dir = RUN_LOG_DIR / tool_name
    tool_dir.mkdir(parents=True, exist_ok=True)
    log_path = tool_dir / f"{run_id}.log"

    def iter_output():
        start = time.time()
        status = "unknown"
        duration_actual = 0.0
        parsed = {}
        parsed_summary = {}
        timed_out = False
        TOOL_STATS.setdefault(tool_name, {})
        TOOL_STATS[tool_name]["running"] = 1
        tool_running.inc()
        
        cmd = ["sudo", str(tool_path)]
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, bufsize=1)
        try:
            with open(log_path, "w", encoding="utf-8") as lf:
                while True:
                    line = proc.stdout.readline()
                    if line == "":
                        break
                    lf.write(line)
                    lf.flush()
                    payload = {"line": line}
                    yield f"data: {json.dumps(payload)}\n\n"
                    if time.time() - start > duration:
                        try:
                            proc.terminate()
                            timed_out = True
                        except Exception:
                            pass
                try:
                    out, _ = proc.communicate(timeout=5)
                except subprocess.TimeoutExpired:
                    proc.kill()
                    out, _ = proc.communicate()
                duration_actual = time.time() - start
                if timed_out:
                    status = "timeout"
                else:
                    status = "success" if proc.returncode == 0 else "error"
                try:
                    full = Path(log_path).read_text(encoding="utf-8")
                except Exception:
                    full = ""
                parsed = parse_output_metrics(full)
                parsed_summary = summarize_tool_metrics(tool_name, parsed, duration_actual) if parsed else {}
                
                tool_run_count.labels(tool=tool_name, status=status).inc()
                tool_last_success.labels(tool=tool_name).set(1 if status == "success" else 0)
                TOOL_STATS[tool_name]["run_count"] = TOOL_STATS[tool_name].get("run_count", 0) + 1
                rcbs = TOOL_STATS[tool_name].setdefault("run_count_by_status", {})
                rcbs[status] = rcbs.get(status, 0) + 1
                TOOL_STATS[tool_name]["last_duration"] = duration_actual
                TOOL_STATS[tool_name]["last_output_bytes"] = len(full.encode("utf-8", "replace"))
                TOOL_STATS[tool_name]["last_success"] = 1 if status == "success" else 0
                TOOL_STATS[tool_name]["last_parsed"] = parsed
                TOOL_STATS[tool_name]["last_parsed_summary"] = parsed_summary
                TOOL_STATS[tool_name]["running"] = 0
                
                config = TOOL_CONFIGS.get(tool_name, DEFAULT_CONFIG)
                final = {
                    "status": status,
                    "duration_seconds": round(duration_actual, 3),
                    "config": config,  # ⭐ 同样在流结束时传输配置
                    "parsed_metrics": parsed,
                    "parsed_summary": parsed_summary,
                    "output_path": str(log_path.relative_to(PROJECT_ROOT)),
                }
                yield f"data: {json.dumps(final)}\n\n"
        except Exception as exc:
            TOOL_STATS[tool_name]["running"] = 0
            err = {"status": "error", "error": str(exc)}
            yield f"data: {json.dumps(err)}\n\n"
        finally:
            tool_running.dec()
    return EventSourceResponse(iter_output())


if __name__ == "__main__":
    import uvicorn

    uvicorn.run("main:app", host="0.0.0.0", port=8000)