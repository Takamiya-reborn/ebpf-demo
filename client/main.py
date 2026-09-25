import os
import time
import json
import re
import asyncio
import logging
import math
import signal
from pathlib import Path

from fastapi import FastAPI, HTTPException, Request, Query
from fastapi.responses import HTMLResponse, PlainTextResponse
from fastapi.staticfiles import StaticFiles
from sse_starlette.sse import EventSourceResponse
from jinja2 import Environment, FileSystemLoader
from prometheus_client import (
    CollectorRegistry,
    Counter,
    Gauge,
    generate_latest,
    CONTENT_TYPE_LATEST,
)

# --- 配置与初始化 ---
CLIENT_ROOT = Path(__file__).resolve().parent
PROJECT_ROOT = CLIENT_ROOT.parent
TOOL_BIN_DIR = PROJECT_ROOT / "my_tools" / "bin"
CONFIG_PATH = CLIENT_ROOT / "config" / "tool_configs.json"


logger = logging.getLogger(__name__)

# 每个工具配置必须包含的字段，缺失时在启动阶段直接报错（fail-fast）
REQUIRED_TOOL_CONFIG_KEYS = ("category", "chart_type")


def load_tool_config():
    try:
        with CONFIG_PATH.open("r", encoding="utf-8") as config_file:
            config = json.load(config_file)
    except (OSError, json.JSONDecodeError) as error:
        raise RuntimeError(f"无法加载工具配置 {CONFIG_PATH}: {error}") from error

    if not isinstance(config, dict):
        raise RuntimeError(f"工具配置必须是 JSON 对象: {CONFIG_PATH}")

    tool_configs = config.get("tools", {})
    default_config = config.get("default", {})
    if not isinstance(tool_configs, dict) or not isinstance(default_config, dict):
        raise RuntimeError(f"工具配置的 tools/default 必须是 JSON 对象: {CONFIG_PATH}")
    for tool_name, tool_config in tool_configs.items():
        missing = [k for k in REQUIRED_TOOL_CONFIG_KEYS if k not in tool_config]
        if missing:
            raise RuntimeError(
                f"工具 {tool_name} 的配置缺少必需字段: {', '.join(missing)} ({CONFIG_PATH})"
            )
    return tool_configs, default_config


TOOL_CONFIGS, DEFAULT_CONFIG = load_tool_config()

app = FastAPI(title="my_tools Dashboard")
app.mount("/static", StaticFiles(directory=CLIENT_ROOT / "static"), name="static")
jinja_env = Environment(
    loader=FileSystemLoader(CLIENT_ROOT / "templates"), autoescape=True
)

registry = CollectorRegistry()
tool_running = Gauge("my_tools_tool_running", "Running count", registry=registry)
TOOL_STATS: dict[str, dict] = {}
tool_runs = Counter(
    "my_tools_tool_run_count",
    "Completed tool runs",
    ["tool", "status"],
    registry=registry,
)
tool_last_duration = Gauge(
    "my_tools_tool_last_duration_seconds",
    "Duration of the last tool run",
    ["tool"],
    registry=registry,
)
tool_last_success = Gauge(
    "my_tools_tool_last_success",
    "Whether the last tool run succeeded",
    ["tool"],
    registry=registry,
)
tool_last_output_bytes = Gauge(
    "my_tools_tool_last_output_bytes",
    "Output bytes from the last tool run",
    ["tool"],
    registry=registry,
)
MAX_DURATION_SECONDS = 300.0
MAX_OUTPUT_BYTES = 2 * 1024 * 1024
MAX_OUTPUT_LINES = 20_000


# --- 工具辅助函数 ---
def get_available_tools():
    if not TOOL_BIN_DIR.exists():
        return []
    configured_tools = set(TOOL_CONFIGS)
    tools = []
    for tool in TOOL_BIN_DIR.iterdir():
        try:
            if tool.name in configured_tools and tool.is_file() and not tool.is_symlink() and os.access(tool, os.X_OK):
                tools.append(tool.name)
        except OSError:
            continue
    return sorted(tools)


def get_tool_path(tool_name: str) -> Path:
    if tool_name not in TOOL_CONFIGS or not re.fullmatch(r"[A-Za-z0-9_.-]+", tool_name):
        raise HTTPException(status_code=400, detail="invalid tool name")
    tool_path = (TOOL_BIN_DIR / tool_name).resolve()
    try:
        tool_path.relative_to(TOOL_BIN_DIR.resolve())
    except ValueError as error:
        raise HTTPException(status_code=400, detail="invalid tool path") from error
    if (
        tool_path.name != tool_name
        or not tool_path.is_file()
        or tool_path.is_symlink()
        or not os.access(tool_path, os.X_OK)
    ):
        raise HTTPException(status_code=404, detail="tool not found")
    return tool_path


def parse_output_metrics(output: str) -> dict[str, float]:
    out = {}
    kv_re = re.compile(r"^\s*([A-Za-z0-9_.-]+)\s*(?:[:=])\s*([-+]?\d*\.?\d+)\s*$")
    for ln in output.splitlines():
        m = kv_re.match(ln)
        if m:
            key, val = m.group(1), float(m.group(2))
            if len(key) <= 128 and math.isfinite(val):
                out[key] = out.get(key, 0.0) + val
        else:
            try:
                j = json.loads(ln)
                if isinstance(j, dict):
                    for k, v in j.items():
                        if (
                            isinstance(k, str)
                            and len(k) <= 128
                            and isinstance(v, (int, float))
                            and math.isfinite(v)
                        ):
                            out[str(k)] = out.get(str(k), 0.0) + v
            except (json.JSONDecodeError, TypeError, ValueError):
                pass
    return out


def summarize_tool_metrics(
    tool_name: str, metrics: dict[str, float], duration: float
) -> dict:
    config = TOOL_CONFIGS.get(tool_name, DEFAULT_CONFIG)
    vals = list(metrics.values())
    if not vals:
        return {}
    return {
        "tool_type": config["category"],
        "count": len(vals),
        "sum": sum(vals),
        "avg": sum(vals) / len(vals) if vals else 0,
        "max": max(vals) if vals else 0,
        "rate_per_sec": len(vals) / duration if duration > 0 else 0,
        "top_keys": [
            {"key": k, "value": v}
            for k, v in sorted(metrics.items(), key=lambda x: x[1], reverse=True)[:5]
        ],
    }


# --- 路由 ---
@app.get("/", response_class=HTMLResponse)
async def index(request: Request):
    tools = [
        {"name": t, "config": TOOL_CONFIGS.get(t, DEFAULT_CONFIG)}
        for t in get_available_tools()
    ]
    return jinja_env.get_template("index.html").render(request=request, tools=tools)


@app.get("/api/tools")
async def api_tools():
    return {"tools": [{"name": t} for t in get_available_tools()]}


@app.get("/metrics", response_class=PlainTextResponse)
async def metrics():
    return PlainTextResponse(generate_latest(registry), media_type=CONTENT_TYPE_LATEST)


@app.get("/api/stats")
async def api_stats():
    return {"tools": TOOL_STATS}


@app.get("/run/stream/{tool_name}")
async def run_tool_stream(
    request: Request,
    tool_name: str,
    duration: float = Query(10.0, gt=0.0, le=MAX_DURATION_SECONDS),
):
    if not math.isfinite(duration):
        raise HTTPException(status_code=400, detail="duration must be finite")
    tool_path = get_tool_path(tool_name)

    async def event_generator():
        # 资源锁与初始化
        # 注意：下面的检查与置位之间不能插入 await，否则会重新引入并发重复运行的竞态
        if TOOL_STATS.get(tool_name, {}).get("running"):
            yield {"data": json.dumps({"status": "error", "error": "Tool is already running"})}
            return

        TOOL_STATS.setdefault(tool_name, {})["running"] = 1
        tool_running.inc()
        start_time = time.monotonic()
        log_content = []
        output_bytes = 0
        output_lines = 0
        output_truncated = False

        process = None
        try:
            process = await asyncio.create_subprocess_exec(
                "sudo",
                "-n",
                "--",
                str(tool_path),
                stdout=asyncio.subprocess.PIPE,
                stderr=asyncio.subprocess.STDOUT,
                start_new_session=(os.name == "posix"),
            )
            while True:
                if await request.is_disconnected():
                    raise asyncio.CancelledError
                if time.monotonic() - start_time >= duration:
                    break
                try:
                    line_bytes = await asyncio.wait_for(
                        process.stdout.readline(), timeout=0.1
                    )
                    if not line_bytes:
                        break
                    line = line_bytes.decode("utf-8", "replace")
                    if output_lines < MAX_OUTPUT_LINES and output_bytes + len(line_bytes) <= MAX_OUTPUT_BYTES:
                        log_content.append(line)
                        output_lines += 1
                        output_bytes += len(line_bytes)
                        yield {"data": json.dumps({"line": line})}
                    elif not output_truncated:
                        output_truncated = True
                        yield {"data": json.dumps({"line": "[output truncated]\n"})}
                except asyncio.TimeoutError:
                    if time.monotonic() - start_time > duration:
                        break
                    continue

            if process is not None and process.returncode is None:
                try:
                    if os.name == "posix":
                        os.killpg(process.pid, signal.SIGTERM)
                    else:
                        process.terminate()
                except ProcessLookupError:
                    pass
                try:
                    await asyncio.wait_for(process.wait(), timeout=2)
                except asyncio.TimeoutError:
                    try:
                        if os.name == "posix":
                            os.killpg(process.pid, signal.SIGKILL)
                        else:
                            process.kill()
                    except ProcessLookupError:
                        pass
                    await process.wait()

            actual_duration = time.monotonic() - start_time
            full_out = "".join(log_content)
            parsed = parse_output_metrics(full_out)
            summary = summarize_tool_metrics(tool_name, parsed, actual_duration)
            tool_runs.labels(tool=tool_name, status="success").inc()
            tool_last_duration.labels(tool=tool_name).set(actual_duration)
            tool_last_success.labels(tool=tool_name).set(1)
            tool_last_output_bytes.labels(tool=tool_name).set(
                len(full_out.encode("utf-8"))
            )
            TOOL_STATS[tool_name].update(
                {"duration_seconds": actual_duration, "status": "success"}
            )

            yield {
                "data": json.dumps(
                    {
                        "status": "success",
                        "duration_seconds": round(actual_duration, 2),
                        "config": TOOL_CONFIGS.get(tool_name, DEFAULT_CONFIG),
                        "parsed_metrics": parsed,
                        "parsed_summary": summary,
                    }
                )
            }
        except asyncio.CancelledError:
            raise
        except Exception as e:
            logger.exception("Tool %s run failed", tool_name)
            tool_runs.labels(tool=tool_name, status="error").inc()
            tool_last_success.labels(tool=tool_name).set(0)
            TOOL_STATS[tool_name].update({"status": "error", "error": str(e)})
            yield {"data": json.dumps({"status": "error", "error": str(e)})}
        finally:
            TOOL_STATS[tool_name]["running"] = 0
            tool_running.dec()
            if process is not None and process.returncode is None:
                try:
                    if os.name == "posix":
                        os.killpg(process.pid, signal.SIGKILL)
                    else:
                        process.kill()
                except ProcessLookupError:
                    pass
                await process.wait()

    return EventSourceResponse(event_generator())


if __name__ == "__main__":
    import argparse

    import uvicorn

    parser = argparse.ArgumentParser(description="Start the my_tools dashboard")
    parser.add_argument("--host", default="127.0.0.1", help="Bind host")
    parser.add_argument("--port", type=int, default=8000, help="Bind port")
    parser.add_argument(
        "--reload", action="store_true", help="Enable auto-reload for development"
    )
    args = parser.parse_args()

    uvicorn.run(
        "main:app" if args.reload else app,
        host=args.host,
        port=args.port,
        reload=args.reload,
    )
