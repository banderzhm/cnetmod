# HTTP/3 性能指标定义

## 核心指标

### QPS（Queries Per Second）
**定义**：每秒成功完成的请求数

**公式**：
```
QPS = successful_requests / elapsed_seconds
```

**测量方法**：
```cpp
auto t1 = steady_clock::now();
auto results = co_await when_all(requests);
auto t2 = steady_clock::now();
auto elapsed = duration_cast<microseconds>(t2 - t1).count() / 1e6;
return successful_requests / elapsed;
```

**参考值**（本地 loopback）：
- HTTP/3 单连接：≥ 10,000 QPS
- HTTP/2 多路复用：≥ 8,000 QPS
- HTTP/1.1 keep-alive：≥ 5,000 QPS

### Throughput（吞吐量）
**定义**：每秒传输的字节数（MB/s）

**公式**：
```
Throughput = total_bytes_sent / elapsed_seconds / 1024 / 1024
```

**参考值**：
- 小文件（1KB）：≥ 50 MB/s
- 大文件（1MB+）：≥ 80 MB/s

### Latency（延迟）
**定义**：从请求发送到收到响应的时间间隔

**统计指标**：
- **avg**：算术平均值
- **P50**：第 50 百分位数（中位数）
- **P95**：第 95 百分位数（长尾）
- **P99**：第 99 百分位数（极端情况）

**计算方法**：
```cpp
auto compute_statistics(std::vector<std::chrono::microseconds>& latencies)
    -> metrics
{
    std::sort(latencies.begin(), latencies.end());

    auto avg = std::accumulate(latencies.begin(), latencies.end(), 0us)
               / latencies.size();

    auto p50 = latencies[latencies.size() * 0.50];
    auto p95 = latencies[latencies.size() * 0.95];
    auto p99 = latencies[latencies.size() * 0.99];

    return {avg, p50, p95, p99};
}
```

### Connection Reuse Rate（连接复用率）
**定义**：复用已有连接的新请求百分比

**公式**：
```
Reuse Rate = reused_connections / total_connection_attempts
```

**理想值**：≥ 90%（连接池配置良好时）

### Memory Usage（内存使用）
**测量方法**：
```bash
# Linux：读取 /proc/self/status
grep VmRSS /proc/self/status

# Windows：GetProcessMemoryInfo API
```

**关键指标**：
- 每连接内存：cnetmod 目标 ≤ 64KB/connection
- 流缓冲内存：≤ 32KB/stream（活跃流）

## 对比基线

| 指标 | cnetmod HTTP/3 | nginx (参考) | 差距 |
|-----|----------------|--------------|-----|
| QPS | 12,000 | 15,000 | -20% |
| Latency P50 | 600us | 400us | +50% |
| Throughput | 85 MB/s | 95 MB/s | -10% |
| Memory/Conn | 58 KB | 72 KB | +23% |

**说明**：cnetmod 目标是生产级性能，但初期 MVP 阶段允许 20-30% 差距，通过后续迭代优化。
