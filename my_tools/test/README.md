# my_tools 测试触发程序

本目录提供针对 `my_tools` 各个监控工具的触发程序，便于手动产生对应系统事件并观察 BPF 监控结果。

## 使用说明

1. 进入测试目录：

```bash
cd /home/takamiya/Document/gitee/libbpf-bootstrap/my_tools/test
```

2. 编译所有触发程序：

```bash
make
```

3. 运行对应触发程序：

```bash
sudo ./bin/<tool>_trigger
```

每个程序会在启动时打印自己的 PID，并尝试触发对应工具监控的系统事件。

## 触发程序列表

- `bin/irq_stat_trigger`
- `bin/disk_delay_trigger`
- `bin/disk_read_delay_trigger`
- `bin/read_stat_trigger`
- `bin/write_stat_trigger`
- `bin/mmap_stat_trigger`
- `bin/oom_stat_trigger`
- `bin/page_fault_stat_trigger`
- `bin/page_swap_stat_trigger`
- `bin/socket_stat_trigger`
- `bin/tcp_connect_trigger`
