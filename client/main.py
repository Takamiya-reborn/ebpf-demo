import os
import time
import json
import re
import uuid
import asyncio
from pathlib import Path
from typing import Dict

from fastapi import FastAPI, HTTPException, Request, Query
from fastapi.responses import HTMLResponse, JSONResponse
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
    return tool_configs, default_config


TOOL_CONFIGS, DEFAULT_CONFIG = load_tool_config()

app = FastAPI(title="my_tools Dashboard")
app.mount("/static", StaticFiles(directory=CLIENT_ROOT / "static"), name="static")
jinja_env = Environment(
    loader=FileSystemLoader(CLIENT_ROOT / "templates"), autoescape=True
)

registry = CollectorRegistry()
tool_running = Gauge("my_tools_tool_running", "Running count", registry=registry)
TOOL_STATS: Dict[str, Dict] = {}


# --- 工具辅助函数 ---
def get_available_tools():
    if not TOOL_BIN_DIR.exists():
        return []
    return sorted([t.name for t in TOOL_BIN_DIR.iterdir() if os.access(t, os.X_OK)])


def parse_output_metrics(output: str) -> Dict[str, float]:
    out = {}
    kv_re = re.compile(r"^\s*([A-Za-z0-9_.-]+)\s*(?:[:=])\s*([-+]?\d*\.?\d+)\s*$")
    for ln in output.splitlines():
        m = kv_re.match(ln)
        if m:
            key, val = m.group(1), float(m.group(2))
            out[key] = out.get(key, 0.0) + val
        else:
            try:
                j = json.loads(ln)
                if isinstance(j, dict):
                    for k, v in j.items():
                        if isinstance(v, (int, float)):
                            out[str(k)] = out.get(str(k), 0.0) + v
            except:
                pass
    return out


def summarize_tool_metrics(
    tool_name: str, metrics: Dict[str, float], duration: float
) -> Dict:
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


@app.get("/run/stream/{tool_name}")
async def run_tool_stream(tool_name: str, duration: float = Query(10.0)):
    tool_path = TOOL_BIN_DIR / tool_name
    if not tool_path.exists():
        raise HTTPException(status_code=404)

    async def event_generator():
        # 资源锁与初始化
        if TOOL_STATS.get(tool_name, {}).get("running"):
            yield {"data": json.dumps({"error": "Tool is already running"})}
            return

        TOOL_STATS.setdefault(tool_name, {})["running"] = 1
        tool_running.inc()
        start_time = asyncio.get_event_loop().time()
        log_content = []

        # 核心：异步执行子进程
        process = await asyncio.create_subprocess_exec(
            "sudo",
            "-n",
            str(tool_path),
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.STDOUT,
        )

        try:
            while True:
                try:
                    line_bytes = await asyncio.wait_for(
                        process.stdout.readline(), timeout=0.1
                    )
                    if not line_bytes:
                        break
                    line = line_bytes.decode("utf-8", "replace")
                    log_content.append(line)
                    yield {"data": json.dumps({"line": line})}
                except asyncio.TimeoutError:
                    if asyncio.get_event_loop().time() - start_time > duration:
                        break
                    continue

            if process.returncode is None:
                process.terminate()

            actual_duration = asyncio.get_event_loop().time() - start_time
            full_out = "".join(log_content)
            parsed = parse_output_metrics(full_out)
            summary = summarize_tool_metrics(tool_name, parsed, actual_duration)

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
        except Exception as e:
            yield {"data": json.dumps({"status": "error", "error": str(e)})}
        finally:
            TOOL_STATS[tool_name]["running"] = 0
            tool_running.dec()
            if process.returncode is None:
                process.kill()

    return EventSourceResponse(event_generator())


if __name__ == "__main__":
    import argparse

    import uvicorn

    parser = argparse.ArgumentParser(description="Start the my_tools dashboard")
    parser.add_argument("--host", default="0.0.0.0", help="Bind host")
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
