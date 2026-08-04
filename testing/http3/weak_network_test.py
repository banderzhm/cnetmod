#!/usr/bin/env python3
"""
Phase 6: Weak Network Testing Suite
=====================================
测试 QUIC/HTTP3 在弱网环境下的性能与可靠性。

测试场景：
1. 丢包率测试 (0.1% / 1% / 5% / 10%)
2. 延迟抖动测试 (50ms / 100ms / 200ms / 500ms)
3. 带宽限制测试 (1Mbps / 10Mbps / 100Mbps)
4. 乱序包测试 (2% / 5% / 10%)
5. 突发流量测试
6. 综合弱网场景 (WiFi / 4G / 3G / Satellite)

使用方法：
    python weak_network_test.py --port 4433 --scenario all
    python weak_network_test.py --port 4433 --scenario loss --loss-rate 5
    python weak_network_test.py --port 4433 --scenario preset --preset wifi_bad
"""

import asyncio
import sys
import time
import json
import argparse
import logging
import statistics
from dataclasses import dataclass, asdict, field
from typing import Optional, List, Tuple
from pathlib import Path

# 添加当前目录到路径，以便导入 network_emulation
sys.path.insert(0, str(Path(__file__).parent))

try:
    from aioquic.asyncio import connect
    from aioquic.quic.configuration import QuicConfiguration
    AIOQUIC_AVAILABLE = True
except ImportError:
    AIOQUIC_AVAILABLE = False

from network_emulation import (
    NetworkEmulator,
    NetworkCondition,
    create_emulator,
    load_presets_from_json,
    PRESETS,
)

logger = logging.getLogger(__name__)

# ---------------------------------------------------------------------------
# Constants
# ---------------------------------------------------------------------------

DEFAULT_HOST = "localhost"
DEFAULT_PORT = 4433
DEFAULT_NUM_REQUESTS = 100
DEFAULT_CONCURRENCY = 10
DEFAULT_PAYLOAD_SIZE = 1024
REQUEST_TIMEOUT_SEC = 10.0
STABILIZATION_WAIT_SEC = 1.0

PASS_THRESHOLD_LIGHT = 0.95   # 轻度弱网成功率阈值
PASS_THRESHOLD_MEDIUM = 0.80  # 中度弱网成功率阈值
PASS_THRESHOLD_HEAVY = 0.50   # 重度弱网成功率阈值


# ---------------------------------------------------------------------------
# Test Result Data Class
# ---------------------------------------------------------------------------

@dataclass
class RequestResult:
    """单次请求的结果"""
    success: bool
    latency_ms: float
    bytes_sent: int
    bytes_received: int
    error: Optional[str] = None


@dataclass
class WeakNetworkTestResult:
    """单个弱网测试场景的聚合结果"""
    scenario_name: str
    condition: NetworkCondition
    passed: bool
    message: str

    # 请求统计
    total_requests: int = 0
    successful_requests: int = 0
    failed_requests: int = 0
    total_bytes_sent: int = 0
    total_bytes_received: int = 0

    # 时间
    elapsed_ms: float = 0
    stabilization_ms: float = 0

    # 性能指标
    qps: float = 0
    throughput_mbps: float = 0

    # 延迟分布
    latency_avg_ms: float = 0
    latency_min_ms: float = 0
    latency_max_ms: float = 0
    latency_p50_ms: float = 0
    latency_p90_ms: float = 0
    latency_p95_ms: float = 0
    latency_p99_ms: float = 0
    latency_stddev_ms: float = 0

    # QUIC 特定指标（占位，可从服务器/客户端统计获取）
    retransmissions: int = 0
    pto_timeouts: int = 0
    congestion_events: int = 0

    # 原始延迟数据
    _latencies: List[float] = field(default_factory=list, repr=False)


# ---------------------------------------------------------------------------
# Latency Calculator
# ---------------------------------------------------------------------------

def calculate_latency_stats(latencies: List[float]) -> dict:
    """计算延迟统计信息"""
    if not latencies:
        return {
            "avg": 0, "min": 0, "max": 0,
            "p50": 0, "p90": 0, "p95": 0, "p99": 0,
            "stddev": 0
        }

    sorted_lat = sorted(latencies)
    n = len(sorted_lat)

    return {
        "avg": statistics.mean(sorted_lat),
        "min": sorted_lat[0],
        "max": sorted_lat[-1],
        "p50": sorted_lat[int(n * 0.50)],
        "p90": sorted_lat[int(n * 0.90)],
        "p95": sorted_lat[int(n * 0.95)],
        "p99": sorted_lat[min(int(n * 0.99), n - 1)],
        "stddev": statistics.stdev(sorted_lat) if n > 1 else 0,
    }


# ---------------------------------------------------------------------------
# QUIC/HTTP3 Client Helpers
# ---------------------------------------------------------------------------

class QuicTestClient:
    """QUIC 测试客户端封装"""

    def __init__(self, host: str, port: int):
        self.host = host
        self.port = port
        self._protocol = None

    async def connect(self) -> bool:
        """建立 QUIC 连接"""
        try:
            config = QuicConfiguration(
                is_client=True,
                alpn_protocols=["h3"]
            )
            config.verify_mode = False

            self._protocol = await connect(
                self.host, self.port, configuration=config
            )
            return True
        except Exception as e:
            logger.error(f"QUIC connection failed: {e}")
            return False

    async def send_request(self, payload_size: int) -> RequestResult:
        """发送单个请求并测量延迟"""
        start_time = time.monotonic()
        payload = b"A" * payload_size

        try:
            stream_id = self._protocol._quic.get_next_available_stream_id()
            self._protocol._quic.send_stream_data(
                stream_id, payload, end_stream=True
            )

            # 等待响应
            deadline = time.monotonic() + REQUEST_TIMEOUT_SEC
            while stream_id not in getattr(self._protocol, "streams_received", {}):
                if time.monotonic() > deadline:
                    raise asyncio.TimeoutError(
                        f"Request timeout after {REQUEST_TIMEOUT_SEC}s"
                    )
                await asyncio.sleep(0.01)

            elapsed_ms = (time.monotonic() - start_time) * 1000
            response_data = self._protocol.streams_received[stream_id][0].get(
                "data", b""
            )

            return RequestResult(
                success=True,
                latency_ms=elapsed_ms,
                bytes_sent=payload_size,
                bytes_received=len(response_data),
            )

        except asyncio.TimeoutError:
            elapsed_ms = (time.monotonic() - start_time) * 1000
            return RequestResult(
                success=False,
                latency_ms=elapsed_ms,
                bytes_sent=payload_size,
                bytes_received=0,
                error="timeout",
            )
        except Exception as e:
            elapsed_ms = (time.monotonic() - start_time) * 1000
            return RequestResult(
                success=False,
                latency_ms=elapsed_ms,
                bytes_sent=payload_size,
                bytes_received=0,
                error=str(e),
            )

    async def close(self):
        """关闭连接"""
        if self._protocol:
            try:
                await self._protocol.close()
            except Exception:
                pass
            self._protocol = None


class EchoTestClient:
    """
    回显协议测试客户端（不依赖 aioquic 高级 API）

    用于基本的连通性验证，使用 UDP socket 模拟 QUIC Initial。
    当 aioquic 不可用时作为 fallback。
    """

    def __init__(self, host: str, port: int):
        self.host = host
        self.port = port

    async def send_request(self, payload_size: int) -> RequestResult:
        """通过 UDP 发送 echo 请求"""
        start_time = time.monotonic()
        payload = b"ECHO" + b"A" * (payload_size - 4)

        try:
            loop = asyncio.get_event_loop()
            sock = _create_udp_socket()
            transport, protocol = await loop.create_datagram_endpoint(
                lambda: _EchoProtocol(payload),
                sock=sock,
            )

            # 等待响应或超时
            deadline = time.monotonic() + REQUEST_TIMEOUT_SEC
            while not protocol.got_response:
                if time.monotonic() > deadline:
                    transport.close()
                    raise asyncio.TimeoutError("Echo timeout")
                await asyncio.sleep(0.01)

            transport.close()
            elapsed_ms = (time.monotonic() - start_time) * 1000

            return RequestResult(
                success=True,
                latency_ms=elapsed_ms,
                bytes_sent=payload_size,
                bytes_received=len(protocol.data),
            )

        except asyncio.TimeoutError:
            elapsed_ms = (time.monotonic() - start_time) * 1000
            return RequestResult(
                success=False, latency_ms=elapsed_ms,
                bytes_sent=payload_size, bytes_received=0, error="timeout",
            )
        except Exception as e:
            elapsed_ms = (time.monotonic() - start_time) * 1000
            return RequestResult(
                success=False, latency_ms=elapsed_ms,
                bytes_sent=payload_size, bytes_received=0, error=str(e),
            )


def _create_udp_socket():
    """创建 UDP socket"""
    import socket
    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.setblocking(False)
    return sock


class _EchoProtocol(asyncio.DatagramProtocol):
    """简单的 echo UDP 协议"""

    def __init__(self, data: bytes):
        self.data_to_send = data
        self.data = b""
        self.got_response = False

    def connection_made(self, transport):
        self.transport = transport
        # 发送数据到目标地址
        transport.sendto(self.data_to_send)

    def datagram_received(self, data, addr):
        self.data = data
        self.got_response = True


# ---------------------------------------------------------------------------
# Weak Network Tester
# ---------------------------------------------------------------------------

class WeakNetworkTester:
    """弱网测试器"""

    def __init__(
        self,
        host: str,
        port: int,
        emulator: NetworkEmulator,
        use_quic: bool = True,
    ):
        self.host = host
        self.port = port
        self.emulator = emulator
        self.use_quic = use_quic and AIOQUIC_AVAILABLE
        self.results: List[WeakNetworkTestResult] = []

    async def run_scenario(
        self,
        name: str,
        condition: NetworkCondition,
        num_requests: int = DEFAULT_NUM_REQUESTS,
        concurrency: int = DEFAULT_CONCURRENCY,
        payload_size: int = DEFAULT_PAYLOAD_SIZE,
        pass_threshold: float = PASS_THRESHOLD_LIGHT,
    ) -> WeakNetworkTestResult:
        """
        运行单个弱网场景。

        Args:
            name:           场景名称
            condition:      网络条件
            num_requests:   总请求数
            concurrency:    并发数
            payload_size:   负载大小（字节）
            pass_threshold: 通过的成功率阈值

        Returns:
            WeakNetworkTestResult
        """
        self._print_scenario_header(name, condition, num_requests, concurrency)

        result = WeakNetworkTestResult(
            scenario_name=name,
            condition=condition,
            passed=False,
            message="",
        )

        # 应用网络条件
        with self.emulator.emulate(condition):
            # 等待网络条件生效
            await asyncio.sleep(STABILIZATION_WAIT_SEC)
            result.stabilization_ms = STABILIZATION_WAIT_SEC * 1000

            # 运行基准测试
            start_time = time.monotonic()

            try:
                request_results = await self._run_benchmark(
                    num_requests, concurrency, payload_size
                )
            except Exception as e:
                logger.error(f"Benchmark failed: {e}")
                request_results = []

            result.elapsed_ms = (time.monotonic() - start_time) * 1000

        # 聚合结果（在 emulate 上下文之外，因为网络条件已清除）
        self._aggregate_results(result, request_results, pass_threshold)
        self._print_result(result)

        self.results.append(result)
        return result

    async def _run_benchmark(
        self,
        num_requests: int,
        concurrency: int,
        payload_size: int,
    ) -> List[RequestResult]:
        """运行基准测试，使用信号量控制并发"""
        semaphore = asyncio.Semaphore(concurrency)
        results: List[RequestResult] = []

        if self.use_quic:
            # 使用 QUIC 客户端
            client = QuicTestClient(self.host, self.port)
            connected = await client.connect()
            if not connected:
                logger.warning("QUIC connection failed, falling back to echo client")
                self.use_quic = False

        async def _do_request(idx: int) -> RequestResult:
            async with semaphore:
                if self.use_quic:
                    return await client.send_request(payload_size)
                else:
                    echo_client = EchoTestClient(self.host, self.port)
                    return await echo_client.send_request(payload_size)

        # 创建所有请求任务
        tasks = [
            asyncio.create_task(_do_request(i))
            for i in range(num_requests)
        ]

        # 等待完成
        done = await asyncio.gather(*tasks, return_exceptions=True)

        for item in done:
            if isinstance(item, RequestResult):
                results.append(item)
            elif isinstance(item, Exception):
                results.append(RequestResult(
                    success=False, latency_ms=0,
                    bytes_sent=payload_size, bytes_received=0,
                    error=str(item),
                ))
            else:
                results.append(RequestResult(
                    success=False, latency_ms=0,
                    bytes_sent=payload_size, bytes_received=0,
                    error="unknown",
                ))

        # 关闭 QUIC 连接
        if self.use_quic:
            await client.close()

        return results

    def _aggregate_results(
        self,
        result: WeakNetworkTestResult,
        request_results: List[RequestResult],
        pass_threshold: float,
    ):
        """聚合请求结果到测试报告"""
        result.total_requests = len(request_results)
        result.successful_requests = sum(1 for r in request_results if r.success)
        result.failed_requests = result.total_requests - result.successful_requests
        result.total_bytes_sent = sum(r.bytes_sent for r in request_results)
        result.total_bytes_received = sum(
            r.bytes_received for r in request_results
        )

        # 收集成功请求的延迟
        latencies = [r.latency_ms for r in request_results if r.success]
        result._latencies = latencies

        # 计算吞吐量指标
        if result.elapsed_ms > 0:
            result.qps = result.successful_requests / (result.elapsed_ms / 1000)
            result.throughput_mbps = (
                (result.total_bytes_received * 8) / (result.elapsed_ms / 1000) / 1e6
            )

        # 计算延迟统计
        if latencies:
            stats = calculate_latency_stats(latencies)
            result.latency_avg_ms = stats["avg"]
            result.latency_min_ms = stats["min"]
            result.latency_max_ms = stats["max"]
            result.latency_p50_ms = stats["p50"]
            result.latency_p90_ms = stats["p90"]
            result.latency_p95_ms = stats["p95"]
            result.latency_p99_ms = stats["p99"]
            result.latency_stddev_ms = stats["stddev"]

        # 判断是否通过
        if result.total_requests > 0:
            success_rate = result.successful_requests / result.total_requests
            if success_rate >= pass_threshold:
                result.passed = True
                result.message = f"OK ({success_rate * 100:.1f}% success)"
            else:
                result.message = (
                    f"FAIL ({success_rate * 100:.1f}% success, "
                    f"expected >= {pass_threshold * 100:.0f}%)"
                )
        else:
            result.message = "FAIL (no requests completed)"

    def _print_scenario_header(
        self,
        name: str,
        condition: NetworkCondition,
        num_requests: int,
        concurrency: int,
    ):
        """打印场景标题"""
        print(f"\n{'=' * 64}")
        print(f"  Scenario : {name}")
        print(f"  Condition: {condition.describe()}")
        print(f"  Requests : {num_requests}, Concurrency: {concurrency}")
        print(f"{'=' * 64}")

    def _print_result(self, result: WeakNetworkTestResult):
        """打印测试结果"""
        status_icon = "PASS" if result.passed else "FAIL"
        print(f"\n  Results:")
        print(f"    Success rate : {result.successful_requests}/{result.total_requests}")
        print(f"    QPS          : {result.qps:.2f}")
        print(f"    Throughput   : {result.throughput_mbps:.3f} Mbps")
        print(f"    Elapsed      : {result.elapsed_ms:.1f} ms")
        if result.successful_requests > 0:
            print(f"    Latency avg  : {result.latency_avg_ms:.2f} ms")
            print(f"    Latency P50  : {result.latency_p50_ms:.2f} ms")
            print(f"    Latency P95  : {result.latency_p95_ms:.2f} ms")
            print(f"    Latency P99  : {result.latency_p99_ms:.2f} ms")
            print(f"    Latency min  : {result.latency_min_ms:.2f} ms")
            print(f"    Latency max  : {result.latency_max_ms:.2f} ms")
        if result.failed_requests > 0:
            print(f"    Failed       : {result.failed_requests}")
        print(f"    Status       : [{status_icon}] {result.message}")

    # -----------------------------------------------------------------------
    # Result Persistence
    # -----------------------------------------------------------------------

    def save_results(self, filename: str = "weak_network_results.json"):
        """保存结果到 JSON 文件"""
        output = {
            "version": "1.0",
            "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"),
            "host": self.host,
            "port": self.port,
            "quic_client": self.use_quic,
            "total_scenarios": len(self.results),
            "passed_scenarios": sum(1 for r in self.results if r.passed),
            "results": [],
        }

        for r in self.results:
            output["results"].append({
                "scenario": r.scenario_name,
                "condition": asdict(r.condition),
                "passed": r.passed,
                "message": r.message,
                "metrics": {
                    "total_requests": r.total_requests,
                    "successful_requests": r.successful_requests,
                    "failed_requests": r.failed_requests,
                    "total_bytes_sent": r.total_bytes_sent,
                    "total_bytes_received": r.total_bytes_received,
                    "elapsed_ms": round(r.elapsed_ms, 2),
                    "qps": round(r.qps, 2),
                    "throughput_mbps": round(r.throughput_mbps, 4),
                    "latency_avg_ms": round(r.latency_avg_ms, 2),
                    "latency_min_ms": round(r.latency_min_ms, 2),
                    "latency_max_ms": round(r.latency_max_ms, 2),
                    "latency_p50_ms": round(r.latency_p50_ms, 2),
                    "latency_p90_ms": round(r.latency_p90_ms, 2),
                    "latency_p95_ms": round(r.latency_p95_ms, 2),
                    "latency_p99_ms": round(r.latency_p99_ms, 2),
                    "latency_stddev_ms": round(r.latency_stddev_ms, 2),
                },
            })

        with open(filename, "w", encoding="utf-8") as f:
            json.dump(output, f, indent=2, ensure_ascii=False)

        print(f"\nResults saved to {filename}")

    def print_summary(self):
        """打印所有场景的总结表格"""
        print(f"\n{'=' * 72}")
        print("  Weak Network Test Summary")
        print(f"{'=' * 72}")
        print(f"  {'Status':<8} {'Scenario':<24} {'QPS':>8} {'P95(ms)':>10} {'Succ%':>8}")
        print(f"  {'-' * 64}")

        for r in self.results:
            status = "PASS" if r.passed else "FAIL"
            succ_pct = (
                (r.successful_requests / r.total_requests * 100)
                if r.total_requests > 0 else 0
            )
            print(
                f"  [{status}]  {r.scenario_name:<22} "
                f"{r.qps:>8.1f} {r.latency_p95_ms:>10.2f} {succ_pct:>7.1f}%"
            )

        passed = sum(1 for r in self.results if r.passed)
        total = len(self.results)
        print(f"  {'-' * 64}")
        print(f"  Total: {passed}/{total} passed")
        print(f"{'=' * 72}\n")


# ---------------------------------------------------------------------------
# Scenario Runners
# ---------------------------------------------------------------------------

async def run_all_scenarios(host: str, port: int, **kwargs):
    """运行所有预设的弱网场景"""
    emulator = create_emulator(port=port)
    tester = WeakNetworkTester(host, port, emulator, **kwargs)

    scenarios: List[Tuple[str, NetworkCondition, float]] = [
        # (名称, 条件, 通过阈值)

        # --- 丢包率测试 ---
        ("Loss 0.1%", PRESETS["loss_0.1pct"], PASS_THRESHOLD_LIGHT),
        ("Loss 1%",   PRESETS["loss_1pct"],   PASS_THRESHOLD_LIGHT),
        ("Loss 5%",   PRESETS["loss_5pct"],   PASS_THRESHOLD_MEDIUM),
        ("Loss 10%",  PRESETS["loss_10pct"],  PASS_THRESHOLD_HEAVY),

        # --- 延迟测试 ---
        ("Delay 50ms",  PRESETS["delay_50ms"],  PASS_THRESHOLD_LIGHT),
        ("Delay 100ms", PRESETS["delay_100ms"], PASS_THRESHOLD_LIGHT),
        ("Delay 200ms", PRESETS["delay_200ms"], PASS_THRESHOLD_MEDIUM),
        ("Delay 500ms", PRESETS["delay_500ms"], PASS_THRESHOLD_MEDIUM),

        # --- 带宽限制 ---
        ("Bandwidth 1Mbps",   PRESETS["bw_1mbps"],   PASS_THRESHOLD_LIGHT),
        ("Bandwidth 10Mbps",  PRESETS["bw_10mbps"],  PASS_THRESHOLD_LIGHT),
        ("Bandwidth 100Mbps", PRESETS["bw_100mbps"], PASS_THRESHOLD_LIGHT),

        # --- 乱序测试 ---
        ("Reorder 2%",  PRESETS["reorder_2pct"],  PASS_THRESHOLD_LIGHT),
        ("Reorder 5%",  PRESETS["reorder_5pct"],  PASS_THRESHOLD_MEDIUM),
        ("Reorder 10%", PRESETS["reorder_10pct"], PASS_THRESHOLD_HEAVY),

        # --- 综合场景 ---
        ("WiFi Good",   PRESETS["wifi_good"],   PASS_THRESHOLD_LIGHT),
        ("WiFi Bad",    PRESETS["wifi_bad"],     PASS_THRESHOLD_MEDIUM),
        ("Mobile 4G",   PRESETS["mobile_4g"],    PASS_THRESHOLD_LIGHT),
        ("Mobile 3G",   PRESETS["mobile_3g"],    PASS_THRESHOLD_MEDIUM),
        ("Satellite",   PRESETS["satellite"],     PASS_THRESHOLD_HEAVY),
    ]

    for name, condition, threshold in scenarios:
        await tester.run_scenario(
            name, condition, pass_threshold=threshold
        )

    tester.print_summary()
    tester.save_results()
    return tester


async def run_loss_scenarios(host: str, port: int, **kwargs):
    """仅运行丢包场景"""
    emulator = create_emulator(port=port)
    tester = WeakNetworkTester(host, port, emulator, **kwargs)

    loss_configs = [
        (0.1,  PASS_THRESHOLD_LIGHT),
        (1.0,  PASS_THRESHOLD_LIGHT),
        (5.0,  PASS_THRESHOLD_MEDIUM),
        (10.0, PASS_THRESHOLD_HEAVY),
    ]

    for loss_rate, threshold in loss_configs:
        condition = NetworkCondition(loss_rate=loss_rate)
        await tester.run_scenario(
            f"Loss {loss_rate}%", condition, pass_threshold=threshold
        )

    tester.print_summary()
    tester.save_results("loss_test_results.json")
    return tester


async def run_delay_scenarios(host: str, port: int, **kwargs):
    """仅运行延迟场景"""
    emulator = create_emulator(port=port)
    tester = WeakNetworkTester(host, port, emulator, **kwargs)

    delay_configs = [
        (50,  5,  PASS_THRESHOLD_LIGHT),
        (100, 10, PASS_THRESHOLD_LIGHT),
        (200, 20, PASS_THRESHOLD_MEDIUM),
        (500, 50, PASS_THRESHOLD_MEDIUM),
    ]

    for delay_ms, jitter_ms, threshold in delay_configs:
        condition = NetworkCondition(delay_ms=delay_ms, jitter_ms=jitter_ms)
        await tester.run_scenario(
            f"Delay {delay_ms}ms", condition, pass_threshold=threshold
        )

    tester.print_summary()
    tester.save_results("delay_test_results.json")
    return tester


async def run_bandwidth_scenarios(host: str, port: int, **kwargs):
    """仅运行带宽限制场景"""
    emulator = create_emulator(port=port)
    tester = WeakNetworkTester(host, port, emulator, **kwargs)

    bw_configs = [
        (1000,   "1Mbps"),
        (10000,  "10Mbps"),
        (100000, "100Mbps"),
    ]

    for bw_kbps, label in bw_configs:
        condition = NetworkCondition(bandwidth_kbps=bw_kbps)
        await tester.run_scenario(
            f"Bandwidth {label}", condition, pass_threshold=PASS_THRESHOLD_LIGHT
        )

    tester.print_summary()
    tester.save_results("bandwidth_test_results.json")
    return tester


async def run_reorder_scenarios(host: str, port: int, **kwargs):
    """仅运行乱序场景"""
    emulator = create_emulator(port=port)
    tester = WeakNetworkTester(host, port, emulator, **kwargs)

    reorder_configs = [
        (2.0,  5, PASS_THRESHOLD_LIGHT),
        (5.0,  3, PASS_THRESHOLD_MEDIUM),
        (10.0, 2, PASS_THRESHOLD_HEAVY),
    ]

    for rate, gap, threshold in reorder_configs:
        condition = NetworkCondition(reorder_rate=rate, reorder_gap=gap)
        await tester.run_scenario(
            f"Reorder {rate}%", condition, pass_threshold=threshold
        )

    tester.print_summary()
    tester.save_results("reorder_test_results.json")
    return tester


async def run_preset_scenario(host: str, port: int, preset_name: str, **kwargs):
    """运行指定的预设场景"""
    emulator = create_emulator(port=port)
    tester = WeakNetworkTester(host, port, emulator, **kwargs)

    if preset_name not in PRESETS:
        print(f"ERROR: Unknown preset '{preset_name}'")
        print(f"Available presets: {', '.join(sorted(PRESETS.keys()))}")
        sys.exit(1)

    condition = PRESETS[preset_name]
    await tester.run_scenario(preset_name, condition)

    tester.print_summary()
    tester.save_results(f"preset_{preset_name}_results.json")
    return tester


async def run_custom_scenario(
    host: str, port: int, condition: NetworkCondition, **kwargs
):
    """运行自定义场景"""
    emulator = create_emulator(port=port)
    tester = WeakNetworkTester(host, port, emulator, **kwargs)

    await tester.run_scenario("Custom", condition)

    tester.print_summary()
    tester.save_results("custom_scenario_results.json")
    return tester


async def run_burst_scenario(host: str, port: int, **kwargs):
    """突发流量测试：瞬间发送 10x 正常流量"""
    emulator = create_emulator(port=port)
    tester = WeakNetworkTester(host, port, emulator, **kwargs)

    # 阶段1：正常流量
    normal_condition = NetworkCondition(delay_ms=20, jitter_ms=5)
    with emulator.emulate(normal_condition):
        await asyncio.sleep(0.5)

        # 正常阶段：10 个请求
        client = EchoTestClient(host, port)
        normal_results = []
        for _ in range(10):
            r = await client.send_request(1024)
            normal_results.append(r)

        # 突发阶段：同时发送 100 个请求
        burst_start = time.monotonic()
        tasks = [client.send_request(1024) for _ in range(100)]
        burst_results = await asyncio.gather(*tasks, return_exceptions=True)
        burst_elapsed = (time.monotonic() - burst_start) * 1000

    # 统计
    normal_ok = sum(1 for r in normal_results if r.success)
    burst_ok = sum(
        1 for r in burst_results
        if isinstance(r, RequestResult) and r.success
    )

    result = WeakNetworkTestResult(
        scenario_name="Burst Traffic",
        condition=NetworkCondition(delay_ms=20, jitter_ms=5),
        passed=(burst_ok / 100 >= 0.50),
        message=f"Normal: {normal_ok}/10, Burst: {burst_ok}/100",
        total_requests=110,
        successful_requests=normal_ok + burst_ok,
        failed_requests=110 - normal_ok - burst_ok,
        elapsed_ms=burst_elapsed,
    )

    tester.results.append(result)
    print(f"\nBurst Traffic Test:")
    print(f"  Normal phase : {normal_ok}/10 success")
    print(f"  Burst phase  : {burst_ok}/100 success ({burst_elapsed:.0f}ms)")
    print(f"  Status       : [{'PASS' if result.passed else 'FAIL'}]")

    tester.print_summary()
    tester.save_results("burst_test_results.json")
    return tester


# ---------------------------------------------------------------------------
# Scenario Dispatch
# ---------------------------------------------------------------------------

SCENARIO_RUNNERS = {
    "all":       run_all_scenarios,
    "loss":      run_loss_scenarios,
    "delay":     run_delay_scenarios,
    "bandwidth": run_bandwidth_scenarios,
    "reorder":   run_reorder_scenarios,
    "burst":     run_burst_scenario,
}


# ---------------------------------------------------------------------------
# Main Entry
# ---------------------------------------------------------------------------

def main():
    """主入口"""
    parser = argparse.ArgumentParser(
        description="Phase 6: Weak Network Test Suite for QUIC/HTTP3",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="""
Examples:
  %(prog)s --scenario all
  %(prog)s --scenario loss
  %(prog)s --scenario delay --port 8443
  %(prog)s --scenario preset --preset wifi_bad
  %(prog)s --scenario custom --loss-rate 3.0 --delay-ms 150
        """,
    )

    parser.add_argument("--host", default=DEFAULT_HOST, help="Server host")
    parser.add_argument(
        "--port", type=int, default=DEFAULT_PORT, help="Server port"
    )
    parser.add_argument(
        "--scenario",
        choices=list(SCENARIO_RUNNERS.keys()) + ["preset", "custom"],
        default="all",
        help="Test scenario to run (default: all)",
    )
    parser.add_argument(
        "--preset", help="Preset condition name (for --scenario preset)"
    )
    parser.add_argument("--loss-rate", type=float, help="Custom loss rate (%%)")
    parser.add_argument("--delay-ms", type=float, help="Custom delay (ms)")
    parser.add_argument(
        "--bandwidth-kbps", type=int, help="Custom bandwidth (kbps)"
    )
    parser.add_argument(
        "--requests", type=int, default=DEFAULT_NUM_REQUESTS,
        help=f"Number of requests (default: {DEFAULT_NUM_REQUESTS})"
    )
    parser.add_argument(
        "--concurrency", type=int, default=DEFAULT_CONCURRENCY,
        help=f"Concurrency level (default: {DEFAULT_CONCURRENCY})"
    )
    parser.add_argument(
        "--no-quic", action="store_true",
        help="Disable QUIC client, use echo client instead"
    )
    parser.add_argument(
        "--json-presets", type=str,
        help="Load presets from JSON file instead of built-in"
    )
    parser.add_argument(
        "-v", "--verbose", action="store_true", help="Verbose output"
    )

    args = parser.parse_args()

    # 配置日志
    log_level = logging.DEBUG if args.verbose else logging.INFO
    logging.basicConfig(
        level=log_level,
        format="%(asctime)s %(levelname)-8s %(message)s",
        datefmt="%H:%M:%S",
    )

    # 检查 aioquic 可用性
    if not AIOQUIC_AVAILABLE:
        logger.warning(
            "aioquic not installed. QUIC client disabled. "
            "Install with: pip install aioquic"
        )

    # 加载外部 JSON 预设
    if args.json_presets:
        global PRESETS
        PRESETS = load_presets_from_json(args.json_presets)
        logger.info(f"Loaded {len(PRESETS)} presets from {args.json_presets}")

    # 构建 kwargs
    kwargs = {"use_quic": not args.no_quic}

    # 分发场景
    if args.scenario in SCENARIO_RUNNERS:
        runner = SCENARIO_RUNNERS[args.scenario]
        asyncio.run(runner(args.host, args.port, **kwargs))

    elif args.scenario == "preset":
        if not args.preset:
            print("ERROR: --preset is required for preset scenario")
            print(f"Available: {', '.join(sorted(PRESETS.keys()))}")
            sys.exit(1)
        asyncio.run(run_preset_scenario(args.host, args.port, args.preset, **kwargs))

    elif args.scenario == "custom":
        condition = NetworkCondition(
            delay_ms=args.delay_ms or 0,
            jitter_ms=(args.delay_ms or 0) * 0.1,
            loss_rate=args.loss_rate or 0,
            bandwidth_kbps=args.bandwidth_kbps,
        )
        asyncio.run(run_custom_scenario(args.host, args.port, condition, **kwargs))


if __name__ == "__main__":
    main()
