# HTTP/3 性能基准测试场景

## 测试场景定义

### 场景 1：并发请求吞吐量测试（Concurrent Throughput）
**目标**：测量高并发场景下的 QPS（每秒请求数）

**测试配置**：
- 并发数：100 / 500 / 1000 / 5000
- 请求总数：10000
- 负载大小：1 KB
- 超时：5s

**预期指标**：
- QPS ≥ 10,000（本地 loopback 环境）
- 成功率 ≥ 99.9%
- CPU 使用率记录

**对比基线**：
- HTTP/1.1 keep-alive
- HTTP/2 多路复用
- QUIC 0-RTT vs 1-RTT

### 场景 2：请求延迟分布（Latency Distribution）
**目标**：测量单次请求的响应时间分布

**测试配置**：
- 并发数：1（串行）
- 请求总数：10000
- 负载大小：100 B
- 超时：3s

**预期指标**：
- P50 ≤ 500us（本地 loopback）
- P95 ≤ 2ms
- P99 ≤ 5ms

**统计方法**：
- 百分位数（Percentile）算法
- 滑动窗口平均
- 直方图可视化

### 场景 3：流复用效率（Stream Multiplexing）
**目标**：对比单连接多流 vs 多连接单流的性能差异

**测试 A（单连接 100 流）**：
```
[Conn]---[Stream 1]---[Stream 2]---...---[Stream 100]
```

**测试 B（100 连接单流）**：
```
[Conn 1]---[Stream 1]
[Conn 2]---[Stream 2]
...
[Conn 100]---[Stream 100]
```

**对比指标**：
- 总耗时
- 内存占用（RSS）
- TCP vs UDP socket 数量
- TLS handshake 次数

### 场景 4：大文件传输带宽（Large Transfer Bandwidth）
**目标**：测量单向最大传输带宽

**测试配置**：
- 并发数：1（单流）
- 文件大小：1 MB / 10 MB / 100 MB
- 测量指标：MB/s
- 拥塞控制：NewReno（默认）

**预期结果**：
- 小文件（1MB）：≥ 100 MB/s
- 中文件（10MB）：≥ 80 MB/s
- 大文件（100MB）：≥ 60 MB/s

### 场景 5：连接池效率（Connection Pool Efficiency）
**目标**：测量 HTTP/3 client 的连接复用能力

**测试步骤**：
1. 打开 N 个并发连接（N=100）
2. 保持 10 秒空闲
3. 关闭一半连接
4. 再次打开新连接（应命中 pool）

**关键指标**：
- 连接复用率
- 闲置超时时间
- 内存泄漏检测

## 自动化测试集成

### CTest 配置
```bash
ctest -R bench_http3_ --output-on-failure
```

### CI/CD 流水线
```yaml
# GitHub Actions 示例
jobs:
  benchmark:
    runs-on: ubuntu-latest
    steps:
      - run: cmake --build build --target h3_benchmark
      - run: ./build/bin/h3_benchmark --runs 3
      - run: cat bench_results.json
      - uses: actions/upload-artifact@v3
        with:
          name: benchmark-results
          path: bench_results.json
```

## 结果可视化

### 推荐工具
- **Grafana**：实时 metrics 展示
- **Plotly**：Python 绘制延迟分布直方图
- **Excel**：快速图表生成

### 数据格式
JSON 输出包含：
```json
{
  "timestamp": "2026-08-03T10:00:00Z",
  "config": { ... },
  "metrics": [
    {
      "scenario": "throughput",
      "concurrency": 100,
      "qps": 12345.67,
      "latency_p50_us": 800,
      "latency_p95_us": 1500,
      "latency_p99_us": 2500
    }
  ]
}
```
