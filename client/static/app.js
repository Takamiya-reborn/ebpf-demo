let metricsChart = null;
let currentEs = null;
const UI = {
    toolSelect: document.getElementById('tool-select'),
    runBtn: document.getElementById('run-btn'),
    status: document.getElementById('run-status'),
    output: document.getElementById('tool-output'),
    summary: document.getElementById('tool-summary'),
    duration: document.getElementById('duration-input')
};
document.getElementById('clear-output-btn').addEventListener('click', () => {
    UI.output.textContent = '';
});
function cleanupConnection() {
    if (currentEs) {
        currentEs.close();
        currentEs = null;
    }
    UI.runBtn.disabled = false;
}
// ⭐ 核心逻辑：判断是否为“纯计数”指标，防止干扰量级差距大的图表
function isSecondaryCountKey(key, config) {
    const k = key.toLowerCase();
    const isCountPattern = k.endsWith('_count') || k.endsWith('_cnt') || k === 'count' || k === 'total';

    // 如果工具类型本身就是计数类（如 irq_stat），我们允许画出来
    // 但如果工具类型是延迟类（latency），我们必须过滤掉计数指标
    if (config.category === 'latency' && isCountPattern) {
        return true;
    }
    return false;
}
function buildChartData(parsedData, config = {}) {
    const labels = [];
    const values = [];
    Object.entries(parsedData ?? {}).forEach(([label, value]) => {
        // ⭐ 过滤掉量级差距过大的计数指标
        if (isSecondaryCountKey(label, config)) {
            return;
        }
        labels.push(label);
        values.push(value);
    });
    return { labels, values };
}

function updateChart(parsedData, config = {}) {
    const canvas = document.getElementById('chart-metrics');
    const ctx = canvas.getContext('2d');
    if (metricsChart) metricsChart.destroy();

    const chartData = buildChartData(parsedData, config);
    const isPie = config.chart_type === 'pie';

    // 调色盘
    const palette = ['#6366f1', '#f43f5e', '#10b981', '#f59e0b', '#8b5cf6', '#06b6d4'];

    metricsChart = new Chart(ctx, {
        type: config.chart_type || 'bar',
        data: {
            labels: chartData.labels,
            datasets: [{
                data: chartData.values,
                backgroundColor: isPie ? palette : 'rgba(99, 102, 241, 0.5)',
                borderColor: isPie ? '#fff' : '#6366f1',
                borderWidth: 2
            }]
        },
        options: {
            responsive: true,
            maintainAspectRatio: false,
            plugins: {
                legend: { display: isPie, position: 'bottom' }
            },
            scales: isPie ? {} : { y: { beginAtZero: true } }
        }
    });
}

function renderSummaryTable(summary) {
    if (!summary || Object.keys(summary).length === 0) {
        const empty = document.createElement('div');
        empty.className = 'empty-state';
        empty.textContent = '无摘要数据';
        return empty;
    }

    const rows = [
        ['工具类型', summary.tool_type],
        ['总样本数', summary.count],
        ['数值总和', summary.sum?.toFixed(2)],
        ['平均值', summary.avg?.toFixed(2)]
    ];
    const fragment = document.createDocumentFragment();
    const table = document.createElement('table');
    table.className = 'summary-table';
    const body = table.appendChild(document.createElement('tbody'));
    rows.forEach(([label, value]) => {
        const row = body.insertRow();
        row.insertCell().textContent = label;
        row.insertCell().textContent = value ?? '';
    });
    fragment.appendChild(table);

    if (summary.top_keys) {
        const title = document.createElement('div');
        title.className = 'detail-title';
        title.textContent = 'Top 5 数据点';
        fragment.appendChild(title);
        const details = document.createElement('table');
        details.className = 'summary-table';
        const detailsBody = details.appendChild(document.createElement('tbody'));
        summary.top_keys.forEach(item => {
            const row = detailsBody.insertRow();
            row.insertCell().textContent = item.key ?? '';
            row.insertCell().textContent = item.value ?? '';
        });
        fragment.appendChild(details);
    }
    return fragment;
}
UI.runBtn.addEventListener('click', () => {
    const tool = UI.toolSelect.value;
    if (!tool) return;
    // 与后端 Query(gt=0, le=300) 约束保持一致，避免 422
    const duration = Math.min(Math.max(parseFloat(UI.duration.value) || 10, 1), 300);
    cleanupConnection();
    UI.runBtn.disabled = true;
    UI.status.textContent = "RUNNING";
    UI.status.className = "status-badge running";
    UI.output.textContent = `[${new Date().toLocaleTimeString()}] 正在启动 ${tool}...\n`;
    currentEs = new EventSource(`/run/stream/${encodeURIComponent(tool)}?duration=${duration}`);
    currentEs.onmessage = (ev) => {
        const data = JSON.parse(ev.data);
        if (data.line) {
            UI.output.textContent += data.line;
            UI.output.scrollTop = UI.output.scrollHeight;
        }
        if (data.status) {
            UI.status.textContent = data.status.toUpperCase();
            UI.status.className = `status-badge ${data.status === 'success' ? 'success' : 'idle'}`;
            if (data.status === 'success') {
                updateChart(data.parsed_metrics, data.config);
                UI.summary.replaceChildren(renderSummaryTable(data.parsed_summary));
            } else if (data.error) {
                UI.output.textContent += `[错误] ${data.error}\n`;
                UI.output.scrollTop = UI.output.scrollHeight;
            }
            cleanupConnection();
        }
    };
    currentEs.onerror = () => {
        UI.status.textContent = "ERROR";
        cleanupConnection();
    };
});

// 初始化工具列表
(async () => {
    try {
        const r = await fetch('/api/tools');
        const data = await r.json();
        UI.toolSelect.replaceChildren(...data.tools.map(t => new Option(t.name, t.name)));
    } catch (e) {
        UI.status.textContent = "OFFLINE";
    }
})();