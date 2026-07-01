# PerfMonitor

为解决多服务器场景下异常难以复现、从宏观指标无法快速定位代码的问题，设计并实现一套从 `/proc` 基础采集到 eBPF 内核追踪再到按需触发的逐层递进诊断系统。

## 架构

```
┌──────────────────────────────────────────────────────────────┐
│                        Manager                               │
│                                                              │
│  gRPC Server :50051  ──→  HostSnapshot (内存聚合, per-host)  │
│                                                              │
│  HTTP :8080                                                  │
│  ├── GET /metrics          Prometheus 格式指标导出            │
│  ├── GET /healthz          健康检查                           │
│  ├── GET /api/events       诊断事件列表 (JSON)                │
│  └── GET /api/flamegraph   按需生成火焰图 SVG                 │
│                                                              │
└────────────────────────┬─────────────────────────────────────┘
                         │ gRPC Push (MonitorInfo protobuf, 每 10s)
┌────────────────────────▼─────────────────────────────────────┐
│                        Worker                                │
│                                                              │
│  Layer 1  MetricCollector  (/proc 轮询)                      │
│  ├── /proc/loadavg         CPU 负载 (1/5/15m)                │
│  ├── /proc/stat            CPU 使用率 (delta-based)          │
│  ├── /proc/meminfo         内存用量                           │
│  ├── /proc/diskstats       磁盘 IOPS / 吞吐 / 延迟            │
│  └── /proc/net/dev         网络 RX/TX Mbps / packets        │
│                                                              │
│  Layer 2  ObserveCollector  (eBPF, 内核态)                   │
│  ├── process_trace.bpf.c   sched_process_exec/exit 追踪      │
│  └── tcp_flow.bpf.c        tcp_sendmsg / tcp_cleanup_rbuf   │
│                            tcp_set_state / tcp_done          │
│                                                              │
│  Layer 3  BpfDiagnoser  (持续 profiling, 循环采集)           │
│  ├── offcputime-bpfcc       Off-CPU 调用栈 → 折叠格式 .data  │
│  └── profile-bpfcc          On-CPU 采样栈 → 折叠格式 .data   │
│                                                              │
│  Layer 4  PerfDiagnoser  (异常触发 profiling)                │
│  └── perf record -g         CPU 持续超阈值 N 秒后触发         │
│                             → perf.data (含调用图)           │
└──────────────────────────────────────────────────────────────┘
```

## 技术栈

| 层级 | 技术 | 实现要点 |
|------|------|----------|
| Layer 1 | `/proc` 文件系统 | delta 计算 CPU%，多设备聚合 disk/net |
| Layer 2 | eBPF (libbpf + CO-RE) | 内核态 map → 用户态 ringbuf 消费 |
| Layer 3 | BCC (offcputime/profile) | `CmdRunner` 子进程管理，zombie 回收，文件轮转 |
| Layer 4 | `perf record` | `/proc/[pid]/stat` 轮询，overflow timer 防抖，条件触发 |
| 通信 | gRPC + Protobuf | Worker push 模型，支持多 Worker 接入同一 Manager |
| 可视化 | FlameGraph (Brendan Gregg) | `perf script \| stackcollapse-perf.pl \| flamegraph.pl` |

## 数据流

```
CPU 异常检测 (Layer 4)
  /proc/[pid]/stat 每 5s 采样
    → CPU% 超过阈值持续 overflow_duration_s 秒
    → 触发 perf record -g -p <pid> -- sleep 5
    → perf.data 写入 /tmp/monitor_system/diagnose/

offcpu / CPU profile (Layer 3)
  offcputime-bpfcc / profile-bpfcc 持续运行 interval_s 秒
    → 输出折叠调用栈到 .data 文件
    → 完成后自动重启下一轮

诊断事件上报
  BpfDiagnoser / PerfDiagnoser 在工具退出 + 文件非空时
    → 写入 events 列表
    → gRPC push 携带 file_path / cpu_percent / timestamp
    → Manager 聚合，保留最近 20 条

火焰图生成 (Manager HTTP)
  GET /api/flamegraph?file=perf_yes_123.data
    → perf script -i <path> | stackcollapse-perf.pl | flamegraph.pl → SVG
  GET /api/flamegraph?file=offcpu__123.data
    → flamegraph.pl < <path> → SVG          (已是折叠格式)
```

## 构建

**依赖**

```bash
# Ubuntu 20.04+
sudo apt install -y build-essential cmake protobuf-compiler libprotobuf-dev \
    libgrpc++-dev protobuf-compiler-grpc \
    libbpf-dev clang linux-headers-$(uname -r) \
    bpfcc-tools linux-tools-$(uname -r)

# 火焰图工具
sudo git clone --depth=1 https://github.com/brendangregg/FlameGraph /opt/FlameGraph
```

**编译**

```bash
git clone <repo> monitor_system && cd monitor_system
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

## 运行

```bash
# Manager (无需 root)
./build/manager/manager

# Worker (需要 root 权限运行 eBPF / perf)
sudo ./build/worker/worker
# 或通过 systemd-run:
sudo systemd-run --unit=monitor_worker \
    --property=LimitMEMLOCK=infinity \
    --property=WorkingDirectory=$(pwd) \
    $(pwd)/build/worker/worker
```

## 配置

编辑 `conf/diagnose_conf.json`：

```json
{
  "offcpu": [{"process_name":"","interval_s":10,"min_block_us":1,"max_block_us":1000000,"limit_file_num":3}],
  "profile": [{"process_name":"","cpus":"0","max_nr":1000,"max_size_kb":4096}],
  "perf": {
    "cpu_threshold_pct": 5,
    "overflow_duration_s": 10,
    "target_processes": ["your_process"]
  },
  "paths": {
    "offcpu_bin":  "/usr/local/bin/offcpu_wrapper",
    "profile_bin": "/usr/local/bin/profile_wrapper",
    "perf_bin":    "/usr/bin/perf",
    "output_dir":  "/tmp/monitor_system/diagnose"
  }
}
```

`process_name` 为空时采集所有进程。`limit_file_num` 控制每类文件最多保留数量（按 mtime 轮转）。

## API

| 路径 | 说明 |
|------|------|
| `GET /metrics` | Prometheus 格式，含四层所有指标 |
| `GET /healthz` | `200 OK` |
| `GET /api/events` | JSON 数组，最近 20 条诊断事件（含文件名、触发 CPU%、时间戳） |
| `GET /api/flamegraph?file=<basename>` | 返回 `image/svg+xml` 火焰图；文件为空或不存在返回 400 |

**示例**

```bash
# 查看所有诊断事件
curl http://localhost:8080/api/events

# 生成 off-CPU 火焰图
curl "http://localhost:8080/api/flamegraph?file=offcpu__1700000000.data" > flamegraph.svg

# 查看实时指标
curl http://localhost:8080/metrics | grep monitor_cpu
```

## Prometheus 指标

| 指标名 | 说明 |
|--------|------|
| `monitor_cpu_load_1m` | CPU 1 分钟负载 |
| `monitor_cpu_used_pct` | CPU 使用率 % |
| `monitor_mem_used_pct` | 内存使用率 % |
| `monitor_disk_iops` | 磁盘 IOPS（所有设备求和） |
| `monitor_net_rx_mbps` / `_tx_mbps` | 网络吞吐 Mbps |
| `monitor_observe_proc_exec` / `_exit` | eBPF 进程创建/退出事件数 |
| `monitor_observe_tcp_flows` | 活跃 TCP 流数量 |
| `monitor_observe_tcp_bytes_sent/recv` | TCP 字节数（累计） |
| `monitor_diagnose_event_count` | Manager 保留的诊断事件总数 |
| `monitor_diagnose_event{type,process,file}` | 各诊断事件触发时的 CPU% |
