let metricsChart = null;

const UI = {
    toolSelect: document.getElementById('tool-select'),
    runBtn: document.getElementById('run-btn'),
    status: document.getElementById('run-status'),
    output: document.getElementById('tool-output'),
    summary: document.getElementById('tool-summary'),
    duration: document.getElementById('duration-input')
};

async function init() {
    try {
        const r = await fetch('/api/tools');
        const data = await r.json();
        data.tools.forEach(t => {
            const opt = document.createElement('option');
            opt.value = t.name;
            opt.textContent = `${t.icon} ${t.name}`;
            UI.toolSelect.appendChild(opt);
        });
    } catch (e) {
        UI.status.textContent = "Backend Offline";
        UI.status.className = "status-badge idle";
    }
}

function updateChart(parsedData) {
    const labels = Object.keys(parsedData);
    const values = Object.values(parsedData);
    const ctx = document.getElementById('chart-metrics').getContext('2d');

    // 创建渐变
    const gradient = ctx.createLinearGradient(0, 0, 0, 400);
    gradient.addColorStop(0, 'rgba(99, 102, 241, 0.5)');
    gradient.addColorStop(1, 'rgba(99, 102, 241, 0)');

    if (metricsChart) {
        metricsChart.data.labels = labels;
        metricsChart.data.datasets[0].data = values;
        metricsChart.update();
    } else {
        metricsChart = new Chart(ctx, {
            type: 'bar',
            data: {
                labels: labels,
                datasets: [{
                    data: values,
                    backgroundColor: gradient,
                    borderColor: '#6366f1',
                    borderWidth: 2,
                    borderRadius: 4,
                    barPercentage: 0.6
                }]
            },
            options: {
                responsive: true,
                maintainAspectRatio: false,
                plugins: { legend: { display: false } },
                scales: {
                    y: { grid: { color: '#f1f5f9' }, border: { display: false } },
                    x: { grid: { display: false } }
                }
            }
        });
    }
}

function resetVisualization() {
    if (metricsChart) {
        metricsChart.destroy();
        metricsChart = null;
    }
    UI.summary.innerHTML = `<div class="empty-state">等待数据流入...</div>`;
}

UI.runBtn.addEventListener('click', async () => {
    resetVisualization();
    const tool = UI.toolSelect.value;
    const duration = UI.duration.value;

    UI.runBtn.disabled = true;
    UI.status.textContent = "RUNNING";
    UI.status.className = "status-badge running";
    UI.output.textContent += `\n[${new Date().toLocaleTimeString()}] Starting ${tool}...\n`;

    try {
        const response = await fetch(`/run/${tool}?duration=${duration}`, { method: 'POST' });
        const result = await response.json();

        UI.status.textContent = result.status === 'error' ? "ERROR" : "COMPLETED";
        UI.status.className = `status-badge ${result.status === 'error' ? 'idle' : 'success'}`;

        UI.output.textContent += result.output;
        UI.output.parentElement.scrollTop = UI.output.parentElement.scrollHeight;

        if (result.parsed_metrics && Object.keys(result.parsed_metrics).length > 0) {
            updateChart(result.parsed_metrics);
            const s = result.parsed_summary;
            UI.summary.innerHTML = `
                <div class="stat-item"><span class="stat-label">样本数</span><span class="stat-value">${s.count || 0}</span></div>
                <div class="stat-item"><span class="stat-label">平均值</span><span class="stat-value">${(s.avg || 0).toFixed(2)}</span></div>
                <div class="stat-item"><span class="stat-label">耗时</span><span class="stat-value">${result.duration_seconds}s</span></div>
            `;
        } else {
            UI.summary.innerHTML = `<div class="empty-state">未检测到可解析的数据流。</div>`;
        }
    } catch (e) {
        UI.status.textContent = "FETCH ERROR";
        UI.status.className = "status-badge idle";
        UI.output.textContent += `\nError: ${e.message}`;
    } finally {
        UI.runBtn.disabled = false;
    }
});

init();