let metricsChart = null;

const UI = {
    toolSelect: document.getElementById('tool-select'),
    runBtn: document.getElementById('run-btn'),
    status: document.getElementById('run-status'),
    output: document.getElementById('tool-output'),
    summary: document.getElementById('tool-summary'),
    duration: document.getElementById('duration-input')
};

// 辅助函数：格式化数值，保留两位小数，处理非数字情况
function formatValue(val) {
    if (typeof val === 'number') {
        return Number.isInteger(val) ? val.toLocaleString() : val.toFixed(2);
    }
    return val || '-';
}

async function init() {
    try {
        const r = await fetch('/api/tools');
        const data = await r.json();
        UI.toolSelect.innerHTML = ''; // 清空选项
        data.tools.forEach(t => {
            const opt = document.createElement('option');
            opt.value = t.name;
            opt.textContent = t.name;
            UI.toolSelect.appendChild(opt);
        });
    } catch (e) {
        UI.status.textContent = "Backend Offline";
        UI.status.className = "status-badge idle";
    }
}

function extractPidFromLabel(label) {
    const match = label.match(/(?:^|_)pid_(\d+)(?:_|$)/i);
    return match ? Number(match[1]) : null;
}

function buildChartData(parsedData, config = {}) {
    const entries = Object.entries(parsedData);
    const pidEntries = [];
    const otherEntries = [];

    // 识别是否为延迟类工具
    const isLatencyTool = (config.category === 'latency');
    entries.forEach(([label, value]) => {
        if (isLatencyTool && (label.toLowerCase().endsWith('_count') || label.toLowerCase().includes('_cnt'))) {
            return;
        }
        const pid = extractPidFromLabel(label);
        if (pid !== null && !Number.isNaN(pid)) {
            pidEntries.push({ pid, label, value });
        } else {
            otherEntries.push({ label, value });
        }
    });
    if (pidEntries.length > 0) {
        pidEntries.sort((a, b) => a.pid - b.pid);
        return {
            labels: pidEntries.map(item => String(item.pid)),
            values: pidEntries.map(item => item.value),
            rawLabels: pidEntries.map(item => item.label),
        };
    }
    return {
        labels: otherEntries.map(item => item.label),
        values: otherEntries.map(item => item.value),
        rawLabels: [],
    };
}

function updateChart(parsedData, config = {}) {
    const chartType = config.chart_type || 'bar';
    const xAxisLabel = config.x_axis || '指标';
    const yAxisLabel = config.y_axis || '数值';
    const unit = config.unit ? ` (${config.unit})` : '';
    // 传入 config 触发过滤逻辑
    const chartData = buildChartData(parsedData, config);
    const canvas = document.getElementById('chart-metrics');
    if (!canvas) return;
    const ctx = canvas.getContext('2d');
    if (metricsChart) {
        metricsChart.destroy();
    }
    metricsChart = new Chart(ctx, {
        type: chartType,
        data: {
            labels: chartData.labels,
            datasets: [{
                label: yAxisLabel,
                data: chartData.values,
                backgroundColor: 'rgba(99, 102, 241, 0.5)',
                borderColor: '#6366f1',
                borderWidth: 2,
                borderRadius: 4
            }]
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            scales: chartType === 'pie' ? {} : {
                y: { beginAtZero: true, title: { display: true, text: yAxisLabel + unit } },
                x: { title: { display: true, text: xAxisLabel } }
            }
        }
    });
}

function resetVisualization() {
    if (metricsChart) {
        metricsChart.destroy();
        metricsChart = null;
    }
    UI.summary.innerHTML = `<div class="empty-state">等待数据流入...</div>`;
}

function formatSummaryRow(label, value) {
    return `<tr><td>${label}</td><td>${value}</td></tr>`;
}

function renderDetailTable(title, rows) {
    return `
        <div class="detail-block" style="margin-top: 15px;">
            <div class="detail-title" style="font-weight: bold; margin-bottom: 8px; color: #1e293b;">${title}</div>
            <table class="summary-table">
                <tbody>
                    ${rows.join('')}
                </tbody>
            </table>
        </div>
    `;
}

function renderSummaryTable(summary) {
    if (!summary || Object.keys(summary).length === 0) {
        return `<div class="empty-state">未检测到可解析的数据流。</div>`;
    }

    const mainRows = [];
    const addScalar = (key, label) => {
        const val = summary[key];
        if (val !== undefined && val !== null) { // 严格检查 null
            UI.summary.querySelector('tbody').innerHTML += formatSummaryRow(label, formatValue(val));
        }
    };

    addScalar('tool_type', '工具类型');
    addScalar('count', '样本数');
    addScalar('sum', '总和');
    addScalar('avg', '平均值');
    addScalar('min', '最小值');
    addScalar('max', '最大值');
    addScalar('rate_per_sec', '每秒速率');
    addScalar('total_count', '总计数');
    addScalar('unique_keys', '唯一键数量');
    addScalar('total_interrupts', '中断总数');
    addScalar('total_connections', '连接总数');

    let sections = [];
    sections.push(`
        <div class="summary-table-section">
            <table class="summary-table">
                <thead>
                    <tr><th>指标</th><th>值</th></tr>
                </thead>
                <tbody>${mainRows.join('')}</tbody>
            </table>
        </div>
    `);

    const addDetailBlock = (title, items, formatFn) => {
        if (!items || !items.length) return;
        const rows = items.map(item => {
            const label = item.key || item.name || item.family || item.pid || 'unknown';
            return formatSummaryRow(label, formatFn(item));
        });
        sections.push(renderDetailTable(title, rows));
    };

    addDetailBlock('Top Keys', summary.top_keys, item => `${item.value} <small style="color: #64748b">(${item.percent || 0}%)</small>`);
    addDetailBlock('Latency 分组', summary.latency_groups, item => {
        const unit = item.unit || 'us';
        return `count=${item.count}, avg=${formatValue(item.avg)} ${unit}`;
    });
    addDetailBlock('Socket Family 占比', summary.socket_family_ratio, item => `${item.count} <small style="color: #64748b">(${item.percent}%)</small>`);
    addDetailBlock('Top Socket PIDs', summary.socket_top_pids, item => `${item.count} <small style="color: #64748b">(${item.percent}%)</small>`);
    addDetailBlock('Derived 平均值', summary.derived_averages, item => `count=${item.count}, avg=${formatValue(item.avg)}`);
    addDetailBlock('Top 连接者', summary.top_connectors, item => `${item.value} <small style="color: #64748b">(${item.percent || 0}%)</small>`);

    return sections.join('');
}

UI.runBtn.addEventListener('click', async () => {
    const tool = UI.toolSelect.value;
    const duration = UI.duration.value;
    if (!tool) return;

    resetVisualization();
    UI.runBtn.disabled = true;
    UI.status.textContent = "RUNNING";
    UI.status.className = "status-badge running";
    UI.output.textContent = `[${new Date().toLocaleTimeString()}] Starting ${tool}...\n`;

    try {
        const es = new EventSource(`/run/stream/${tool}?duration=${duration}`);
        es.onmessage = (ev) => {
            try {
                const data = JSON.parse(ev.data);
                if (data.line) {
                    UI.output.textContent += data.line;
                    UI.output.scrollTop = UI.output.scrollHeight;
                }
                if (data.status) {
                    UI.status.textContent = data.status === 'success' ? "COMPLETED" : (data.status === 'timeout' ? 'TIMEOUT' : 'ERROR');
                    UI.status.className = `status-badge ${data.status === 'success' ? 'success' : 'idle'}`;
                    if (data.parsed_metrics && Object.keys(data.parsed_metrics).length > 0) {
                        updateChart(data.parsed_metrics, data.config);
                        UI.summary.innerHTML = renderSummaryTable(data.parsed_summary || {});
                    } else {
                        UI.summary.innerHTML = `<div class="empty-state">采集完成，但未发现可解析的指标数据。</div>`;
                    }
                    es.close();
                    UI.runBtn.disabled = false;
                }
            } catch (e) {
                console.error('Failed parsing stream event', e);
            }
        };
        es.onerror = (err) => {
            UI.status.textContent = "STREAM ERROR";
            UI.status.className = "status-badge idle";
            UI.output.textContent += `\nStream error`;
            try { es.close(); } catch (e) { }
            UI.runBtn.disabled = false;
        };
    } catch (e) {
        UI.status.textContent = "STREAM INIT ERROR";
        UI.status.className = "status-badge idle";
        UI.output.textContent += `\nError: ${e.message}`;
        UI.runBtn.disabled = false;
    }
});

init();