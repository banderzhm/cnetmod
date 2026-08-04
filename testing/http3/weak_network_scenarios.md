# Phase 6: 弱网环境测试场景定义

## 概述

弱网测试验证 QUIC/HTTP3 实现在不完美网络条件下的性能与可靠性。
这对于生产部署至关重要，因为真实网络环境很少是理想的。

本测试套件覆盖以下维度：
- 丢包率（随机/相关性）
- 延迟与抖动
- 带宽限制
- 包乱序
- 突发流量
- 综合弱网场景（WiFi / 4G / 3G / 卫星）

## 测试维度

### 1. 丢包率 (Packet Loss)

**原理**：模拟网络拥塞或无线干扰导致的包丢失

**实现**：
- Linux: `tc qdisc add dev <iface> root netem loss <rate>%`
- Windows: Clumsy `--drop <rate>`

**验证点**：
- 丢包检测（时间/阈值模式）正确触发
- 重传机制正常工作
- 拥塞控制适当响应（cwnd 减小）
- RTT 估计不受影响
- 连接在高丢包下不崩溃

**场景**：

| 名称 | 丢包率 | 预期影响 | 成功标准 |
|------|--------|---------|---------|
| loss_0.1pct | 0.1% | 轻微性能下降 | QPS 下降 < 10%, 成功率 ≥ 95% |
| loss_1pct | 1% | 明显性能下降 | QPS 下降 < 30%, 成功率 ≥ 95% |
| loss_5pct | 5% | 严重性能下降 | 连接不崩溃, 成功率 ≥ 80% |
| loss_10pct | 10% | 极端条件 | 连接不崩溃, 成功率 ≥ 50% |

**QUIC 行为预期**：
- 0.1%: 偶尔触发重传，对吞吐量影响极小
- 1%: 重传频率增加，cwnd 周期性收缩
- 5%: 频繁重传，cwnd 维持在较低水平
- 10%: 连接可能不稳定，PTO 频繁触发

### 2. 延迟与抖动 (Latency & Jitter)

**原理**：模拟地理距离、路由跳数、拥塞队列导致的延迟

**实现**：
- Linux: `tc qdisc add dev <iface> root netem delay <base>ms <jitter>ms`
- Windows: Clumsy `--lag <ms>`

**验证点**：
- PTO 自适应能力（PTO = smoothed_rtt + 4 * rtt_var）
- RTT 估计收敛到真实值
- 不频繁触发伪重传（spurious retransmission）
- ACK delay 正确计算

**场景**：

| 名称 | 基础延迟 | 抖动 | 预期 QPS | 成功标准 |
|------|---------|------|---------|---------|
| delay_50ms | 50ms | ±5ms | > 100 | PTO 正确调整, 成功率 ≥ 95% |
| delay_100ms | 100ms | ±10ms | > 50 | 连接稳定, 成功率 ≥ 95% |
| delay_200ms | 200ms | ±20ms | > 25 | 无伪重传, 成功率 ≥ 80% |
| delay_500ms | 500ms | ±50ms | > 10 | RTT 估计收敛, 成功率 ≥ 80% |

**QUIC 行为预期**：
- 低延迟: RTT 估计快速收敛，PTO 精确
- 高延迟: 初始握手时间增加，但稳态吞吐量不受显著影响
- 高抖动: RTT variance 增大，PTO 退避更保守

### 3. 带宽限制 (Bandwidth Limitation)

**原理**：模拟不同网络类型的带宽约束

**实现**：
- Linux: `tc qdisc add dev <iface> parent 1:1 handle 10: tbf rate <rate>kbit burst 32kbit latency 400ms`
- Windows: dummynet pipe 配置

**验证点**：
- 拥塞控制检测到拥塞（包丢失/ECN）
- cwnd 适当减小
- 吞吐量稳定在带宽限制附近（公平性）
- 不出现缓冲区膨胀（bufferbloat）

**场景**：

| 名称 | 带宽 | 预期吞吐量 | 成功标准 |
|------|------|-----------|---------|
| bw_1mbps | 1 Mbps | ~0.9 Mbps | cwnd 稳定, 成功率 ≥ 95% |
| bw_10mbps | 10 Mbps | ~8 Mbps | 吞吐量接近限制, 成功率 ≥ 95% |
| bw_100mbps | 100 Mbps | ~80 Mbps | 无拥塞崩溃, 成功率 ≥ 95% |

### 4. 包乱序 (Packet Reordering)

**原理**：模拟多路径路由、ECMP 导致的包乱序

**实现**：
- Linux: `tc qdisc add dev <iface> root netem delay <ms>ms reorder <rate>% gap <gap>`
- 注意：乱序需要配合延迟一起使用

**验证点**：
- 乱序重组逻辑正确工作
- 不触发不必要的重传（false loss detection）
- ACK 处理容忍乱序
- reorder tolerance 窗口合理

**场景**：

| 名称 | 乱序率 | 间隔(gap) | 预期影响 | 成功标准 |
|------|--------|----------|---------|---------|
| reorder_2pct | 2% | 5 packets | 轻微性能下降 | 无伪重传, 成功率 ≥ 95% |
| reorder_5pct | 5% | 3 packets | 中等性能下降 | 连接稳定, 成功率 ≥ 80% |
| reorder_10pct | 10% | 2 packets | 严重性能下降 | 不崩溃, 成功率 ≥ 50% |

### 5. 突发流量 (Burst Traffic)

**原理**：模拟瞬间高并发请求，验证拥塞控制的抗突发能力

**验证点**：
- 突发流量期间不触发连接重置
- 拥塞窗口在突发后正确恢复
- 延迟在突发后恢复到正常水平
- 无队列溢出或包丢失

**场景**：

| 阶段 | 请求数 | 并发 | 描述 |
|------|-------|------|------|
| 正常 | 10 | 1 | 基线测量 |
| 突发 | 100 | 100 | 10x 瞬时并发 |

**成功标准**：
- 突发阶段成功率 ≥ 50%
- 突发后延迟恢复到 2x 基线以内

### 6. 综合弱网场景

#### WiFi 网络 (Good)
**特征**：低延迟，极低丢包，高带宽
```
delay=20ms, jitter=5ms, loss=0.1%, bandwidth=50Mbps
```
**预期**：接近理想网络性能，偶尔重传

#### WiFi 网络 (Bad)
**特征**：中等延迟，中等丢包，中等带宽
```
delay=100ms, jitter=50ms, loss=2%, bandwidth=20Mbps, reorder=1%
```
**预期**：明显性能下降，频繁重传

#### 4G 移动网络
**特征**：较高延迟，中等丢包，有限带宽
```
delay=50ms, jitter=20ms, loss=0.5%, bandwidth=10Mbps, reorder=0.5%
```
**预期**：中等性能，适合一般应用

#### 3G 移动网络
**特征**：高延迟，较高丢包，低带宽
```
delay=200ms, jitter=100ms, loss=1%, bandwidth=1Mbps, reorder=1%
```
**预期**：性能受限，但连接稳定

#### 卫星连接
**特征**：极高延迟，高丢包，极低带宽
```
delay=600ms, jitter=100ms, loss=2%, bandwidth=500kbps, reorder=2%
```
**预期**：极端条件，仅适合低带宽应用

## 成功标准

### 通用标准

| 弱网等级 | 成功率阈值 | 适用场景 |
|---------|-----------|---------|
| 轻度 | ≥ 95% | loss ≤ 1%, delay ≤ 100ms, bw ≥ 10Mbps |
| 中度 | ≥ 80% | loss ≤ 5%, delay ≤ 200ms, bw ≥ 1Mbps |
| 重度 | ≥ 50% | loss ≤ 10%, delay ≤ 500ms, bw ≥ 500kbps |

### 额外标准
- 连接不崩溃（所有场景）
- 无死锁或无限重传循环
- 测试结果完整记录（QPS / 延迟 / 吞吐量 / 成功率）

### QUIC 特定标准
- 丢包检测触发时间符合 RFC 9002
- PTO 计算正确（smoothed_rtt + 4 * rtt_var + max_ack_delay）
- 拥塞控制状态转换正确（Slow Start → Congestion Avoidance → Recovery）
- 无伪重传（spurious retransmission rate < 1%）

## 自动化集成

### CTest 配置

在 `testing/http3/CMakeLists.txt` 中：

```cmake
if(Python3_Interpreter_FOUND AND CNETMOD_HAS_QUIC)
    # Phase 6: Weak network tests (requires root/admin for network emulation)

    # 丢包测试
    add_test(NAME weak_network_loss_test
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/weak_network_test.py"
                "--scenario" "loss"
                "--port" "4433"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    set_tests_properties(weak_network_loss_test PROPERTIES
        TIMEOUT 300
        LABELS "weak_network"
        ENVIRONMENT "PATH=${CMAKE_BINARY_DIR}/bin:$ENV{PATH}")

    # 延迟测试
    add_test(NAME weak_network_delay_test
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/weak_network_test.py"
                "--scenario" "delay"
                "--port" "4433"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    set_tests_properties(weak_network_delay_test PROPERTIES
        TIMEOUT 300
        LABELS "weak_network"
        ENVIRONMENT "PATH=${CMAKE_BINARY_DIR}/bin:$ENV{PATH}")
endif()
```

### CI/CD 流水线 (GitHub Actions)

```yaml
jobs:
  weak-network-test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v3

      - name: Install tc (iproute2)
        run: sudo apt-get update && sudo apt-get install -y iproute2

      - name: Install Python dependencies
        run: pip install aioquic

      - name: Build server
        run: cmake --build build --target quic_echo_server

      - name: Start server
        run: |
          ./build/bin/quic_echo_server --port 4433 &
          sleep 2

      - name: Run weak network tests
        run: |
          sudo python testing/http3/weak_network_test.py \
            --scenario all \
            --port 4433

      - name: Upload results
        if: always()
        uses: actions/upload-artifact@v3
        with:
          name: weak-network-results
          path: |
            testing/http3/weak_network_results.json
            testing/http3/loss_test_results.json
            testing/http3/delay_test_results.json
```

## 调优建议

### 如果丢包测试失败
1. 检查 loss_detector 的阈值设置（kPacketThreshold = 3）
2. 验证 PTO 计算逻辑（RFC 9002 Section 6.2.1）
3. 增加重传次数限制
4. 确认 cwnd 最小值不低于 2 * MSS

### 如果延迟测试失败
1. 检查 RTT 估计的 EWMA 平滑算法（RFC 9002 Section 5.3）
2. 验证 PTO 退避机制（exponential backoff, 最大 6 次）
3. 确保 ack_delay 正确计算
4. 检查 initial RTT 默认值（333ms）

### 如果带宽测试失败
1. 检查拥塞控制的 cwnd 增长策略（Slow Start / Congestion Avoidance）
2. 验证 BBR/CUBIC 的带宽探测逻辑
3. 确保 pacing 机制正常工作
4. 检查 MTU 发现和 PMTUD

### 如果乱序测试失败
1. 检查 reorder tolerance 窗口大小
2. 验证 ACK elicitation 逻辑
3. 确认 loss detection 区分丢包和乱序
4. 调整 reordering threshold

## 已知限制

1. **权限要求**
   - Linux tc netem 需要 root 权限或 `CAP_NET_ADMIN`
   - Windows Clumsy 需要管理员权限

2. **平台支持**
   - Windows 上 Clumsy 功能有限（不支持带宽限制和抖动）
   - macOS 暂不支持（需实现 dnctl/pf 后端）

3. **模拟精度**
   - tc netem 的延迟精度约为 ±1ms
   - 高丢包率（>10%）可能导致连接不稳定，超出测试范围
   - 带宽限制在 loopback 接口上效果有限

4. **性能开销**
   - 网络模拟会增加系统开销
   - 高并发场景下可能需要增加系统资源

## 参考资料

- [RFC 9000 - QUIC Transport](https://datatracker.ietf.org/doc/html/rfc9000)
- [RFC 9002 - QUIC Loss Detection and Congestion Control](https://datatracker.ietf.org/doc/html/rfc9002)
- [tc netem man page](https://man7.org/linux/man-pages/man8/tc-netem.8.html)
- [Clumsy - Windows network tool](https://jagt.github.io/clumsy/)
