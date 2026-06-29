import os
import subprocess
import time
import json
import re
from pathlib import Path
from typing import Dict

from fastapi import FastAPI, HTTPException, Request, Query
from fastapi.responses import HTMLResponse, JSONResponse
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

ICON_MAP = {
    "disk_delay": "💽",
    "disk_read_delay": "📥",
    "irq_stat": "⚡",
    "mmap_stat": "🧠",
    "oom_stat": "🔥",
    "page_fault_stat": "📄",
    "page_swap_stat": "🧾",
    "read_stat": "📥",
    "write_stat": "📤",
    "socket_stat": "🌐",
    "tcp_connect": "🔌",
}

STATUS_ICON = {"success": "✅", "error": "❌", "timeout": "⏱️", "unknown": "⚠️"}

ALLOWED_TOOLS = sorted(
    [
        tool.name
        for tool in TOOL_BIN_DIR.iterdir()
        if tool.is_file() and os.access(tool, os.X_OK)
    ]
)

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

# In-memory stats cache to make it easy for the frontend to consume JSON
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


def make_tool_info(name: str) -> Dict[str, str]:
    return {
        "name": name,
        "icon": ICON_MAP.get(name, "🧩"),
        "summary": name.replace("_", " ").capitalize(),
    }


def parse_output_metrics(output: str) -> Dict[str, float]:
    """Try to extract numeric metrics from output.

    Supports JSON object with numeric values, lines like `key: 123`, or token-number pairs.
    """
    out = {}
    if not output:
        return out
    # try JSON
    try:
        j = json.loads(output)
        if isinstance(j, dict):
            for k, v in j.items():
                if isinstance(v, (int, float)):
                    out[str(k)] = float(v)
        if out:
            return out
    except Exception:
        pass

    # key: value lines
    kv_re = re.compile(r"^\s*([A-Za-z0-9_.-]+)\s*(?:[:=])\s*([-+]?\d*\.?\d+(?:[eE][-+]?\d+)?)\s*$")
    for ln in output.splitlines():
        m = kv_re.match(ln)
        if m:
            out[m.group(1)] = float(m.group(2))

    if out:
        return out

    # fallback: find token-number pairs
    num_re = re.compile(r"([A-Za-z0-9_.-]+)?\s*([-+]?\d*\.?\d+(?:[eE][-+]?\d+)?)")
    for m in num_re.finditer(output):
        key = m.group(1) or "value"
        try:
            val = float(m.group(2))
        except Exception:
            continue
        if key in out:
            # if duplicate, append numeric suffix
            i = 1
            while f"{key}_{i}" in out:
                i += 1
            out[f"{key}_{i}"] = val
        else:
            out[key] = val
    return out


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
    tools = [make_tool_info(name) for name in ALLOWED_TOOLS]
    return JSONResponse({"tools": tools})


@app.get("/api/stats")
async def api_stats() -> JSONResponse:
    # return the in-memory TOOL_STATS snapshot
    return JSONResponse({"stats": TOOL_STATS, "running": int(tool_running._value.get())})


@app.post("/run/{tool_name}")
async def run_tool(tool_name: str, duration: float = Query(10.0)) -> JSONResponse:
    if tool_name not in ALLOWED_TOOLS:
        raise HTTPException(status_code=404, detail=f"Unknown tool: {tool_name}")

    tool_path = TOOL_BIN_DIR / tool_name
    if not tool_path.exists():
        raise HTTPException(status_code=404, detail=f"Tool binary not found: {tool_name}")

    # start process and enforce capture duration on server side
    tool_running.inc()
    TOOL_STATS.setdefault(tool_name, {})
    TOOL_STATS[tool_name]["running"] = 1
    start = time.time()
    output = ""
    status = "unknown"
    parsed = {}
    parsed_summary = {}

    try:
        # run tool with sudo by default (assumes sudoers permits nopasswd or user will provide password)
        cmd = ["sudo", str(tool_path)]
        proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        try:
            # wait up to duration seconds for process to finish
            out, err = proc.communicate(timeout=duration)
            output = (out or "") + (err or "")
            duration_actual = time.time() - start
            status = "success" if proc.returncode == 0 else "error"
        except subprocess.TimeoutExpired:
            # still running after capture duration -> terminate gracefully
            try:
                proc.terminate()
                out, err = proc.communicate(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                out, err = proc.communicate()
            output = (out or "") + (err or "")
            duration_actual = time.time() - start
            status = "timeout"

        # parse numeric metrics
        parsed = parse_output_metrics(output)
        if parsed:
            vals = list(parsed.values())
            parsed_summary = {
                "count": len(vals),
                "sum": sum(vals),
                "avg": (sum(vals) / len(vals)) if vals else 0,
            }

        tool_run_count.labels(tool=tool_name, status=status).inc()
        tool_last_success.labels(tool=tool_name).set(1 if status == "success" else 0)
        # update in-memory stats
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

    return JSONResponse(
        {
            "tool": tool_name,
            "status": status,
            "icon": STATUS_ICON.get(status, STATUS_ICON["unknown"]),
            "duration_seconds": round(duration_actual, 3),
            "output": output,
            "parsed_metrics": TOOL_STATS[tool_name].get("last_parsed", {}),
            "parsed_summary": TOOL_STATS[tool_name].get("last_parsed_summary", {}),
        }
    )


if __name__ == "__main__":
    import uvicorn

    uvicorn.run("main:app", host="0.0.0.0", port=8000)
