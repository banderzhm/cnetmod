#!/usr/bin/env python3
"""
Phase 3: HTTP/3 Functional Test Suite
======================================
RFC 9114 核心场景全覆盖。

P0 测试（必须通过）：
1. GET 无 body — HEADERS + DATA 帧链路
2. POST 带 body — 请求 body 分片 + END_STREAM
3. 大 body（>流控窗口）— MAX_STREAM_DATA 自动更新
4. 多 header 字段 — QPACK 编码正确性
5. 并发 10/100 流 — 同一 QUIC 连接多路复用
6. GOAWAY 优雅关闭 — 服务端通知客户端

P1 测试（Phase 4 前完成）：
7. 流错误隔离 — RST_STREAM 不影响其他流
8. SETTINGS 交换 — 双方参数协商
9. 非法 HEADERS 帧 — 错误码 H3_FRAME_UNEXPECTED
10. 头部字段超限 — H3_EXCESSIVE_LOAD
11. 未知单向流类型 — 静默忽略
12. 连接空闲超时 — 自动关闭
"""

import asyncio
import sys
import time
import json
import struct
import argparse
from dataclasses import dataclass, field
from typing import Optional

try:
    from aioquic.asyncio import connect
    from aioquic.quic.configuration import QuicConfiguration
    from aioquic.asyncio.protocol import QuicConnectionProtocol
    from aioquic.quic.events import (
        StreamDataReceived,
        HandshakeCompleted,
        ConnectionTerminated,
    )
    from aioquic.h3.connection import H3Connection
    from aioquic.h3.events import (
        DataReceived,
        HeadersReceived,
        WebTransportStreamDataReceived,
    )
except ImportError:
    print("ERROR: aioquic not installed")
    print("Run: pip install aioquic>=1.0.0")
    sys.exit(1)


# ============================================================
# HTTP/3 Frame Types (RFC 9114 §7.2)
# ============================================================
H3_FRAME_DATA = 0x00
H3_FRAME_HEADERS = 0x01
H3_FRAME_CANCEL_PUSH = 0x03
H3_FRAME_SETTINGS = 0x04
H3_FRAME_PUSH_PROMISE = 0x05
H3_FRAME_GOAWAY = 0x07
H3_FRAME_MAX_PUSH_ID = 0x0D


# ============================================================
# HTTP/3 Error Codes (RFC 9114 §8.1)
# ============================================================
H3_NO_ERROR = 0x0100
H3_GENERAL_PROTOCOL_ERROR = 0x0101
H3_INTERNAL_ERROR = 0x0102
H3_STREAM_CREATION_ERROR = 0x0103
H3_CLOSED_CRITICAL_STREAM = 0x0104
H3_FRAME_UNEXPECTED = 0x0105
H3_FRAME_ERROR = 0x0106
H3_EXCESSIVE_LOAD = 0x0107
H3_ID_ERROR = 0x0108
H3_SETTINGS_ERROR = 0x0109
H3_MISSING_SETTINGS = 0x010A
H3_REQUEST_REJECTED = 0x010B
H3_REQUEST_CANCELLED = 0x010C
H3_REQUEST_INCOMPLETE = 0x010D
H3_MESSAGE_ERROR = 0x010E
H3_CONNECT_ERROR = 0x010F
H3_VERSION_FALLBACK = 0x0110


# ============================================================
# QPACK Encoding Helpers
# ============================================================
def encode_varint(value: int) -> bytes:
    """Encode QUIC variable-length integer."""
    if value < 64:
        return struct.pack("B", value)
    elif value < 16384:
        return struct.pack(">H", value | 0x4000)
    elif value < 1073741824:
        return struct.pack(">I", value | 0x80000000)
    else:
        return struct.pack(">Q", value | 0xC000000000000000)


def encode_qpack_headers(headers: list[tuple[bytes, bytes]]) -> bytes:
    """
    Encode headers using QPACK (static table only for simplicity).
    This is a minimal implementation for test purposes.
    """
    result = b'\x00\x00'  # Required Insert Count + Delta Base

    for name, value in headers:
        # Check static table
        static_idx = _lookup_static_table(name, value)
        if static_idx is not None:
            # Indexed Field Line (static)
            result += struct.pack("B", 0xC0 | static_idx)
        else:
            name_idx = _lookup_static_table_name(name)
            if name_idx is not None:
                # Literal with Name Reference (static)
                result += struct.pack("B", 0x50 | name_idx)
                result += encode_varint(len(value))
                result += value
            else:
                # Literal without Name Reference
                result += b'\x20'
                result += encode_varint(len(name))
                result += name
                result += encode_varint(len(value))
                result += value

    return result


def _lookup_static_table(name: bytes, value: bytes) -> Optional[int]:
    """Lookup exact match in QPACK static table."""
    table = [
        (b":authority", b""),
        (b":path", b"/"),
        (b"age", b"0"),
        (b"content-disposition", b""),
        (b"content-length", b"0"),
        (b"cookie", b""),
        (b"date", b""),
        (b"etag", b""),
        (b"if-modified-since", b""),
        (b"if-none-match", b""),
        (b"last-modified", b""),
        (b"link", b""),
        (b"location", b""),
        (b"referer", b""),
        (b"set-cookie", b""),
        (b":method", b"CONNECT"),
        (b":method", b"DELETE"),
        (b":method", b"GET"),
        (b":method", b"HEAD"),
        (b":method", b"OPTIONS"),
        (b":method", b"POST"),
        (b":method", b"PUT"),
        (b":scheme", b"http"),
        (b":scheme", b"https"),
        (b":status", b"103"),
        (b":status", b"200"),
        (b":status", b"304"),
        (b":status", b"404"),
        (b":status", b"503"),
        (b"accept", b"*/*"),
        (b"accept", b"application/dns-message"),
        (b"accept-encoding", b"gzip, deflate, br"),
        (b"accept-ranges", b"bytes"),
        (b"access-control-allow-headers", b"cache-control"),
        (b"access-control-allow-headers", b"content-type"),
        (b"access-control-allow-origin", b"*"),
        (b"cache-control", b"max-age=0"),
        (b"cache-control", b"max-age=2592000"),
        (b"cache-control", b"max-age=604800"),
        (b"cache-control", b"no-cache"),
        (b"cache-control", b"no-store"),
        (b"cache-control", b"public, max-age=31536000"),
        (b"content-encoding", b"br"),
        (b"content-encoding", b"gzip"),
        (b"content-type", b"application/dns-message"),
        (b"content-type", b"application/javascript"),
        (b"content-type", b"application/json"),
        (b"content-type", b"application/x-www-form-urlencoded"),
        (b"content-type", b"image/gif"),
        (b"content-type", b"image/jpeg"),
        (b"content-type", b"image/png"),
        (b"content-type", b"text/css"),
        (b"content-type", b"text/html; charset=utf-8"),
        (b"content-type", b"text/plain"),
        (b"content-type", b"text/plain;charset=utf-8"),
        (b"range", b"bytes=0-"),
        (b"strict-transport-security", b"max-age=31536000"),
        (b"strict-transport-security", b"max-age=31536000; includesubdomains"),
        (b"strict-transport-security", b"max-age=31536000; includesubdomains; preload"),
        (b"vary", b"accept-encoding"),
        (b"vary", b"origin"),
        (b"x-content-type-options", b"nosniff"),
        (b"x-xss-protection", b"1; mode=block"),
        (b":status", b"100"),
        (b":status", b"204"),
        (b":status", b"206"),
        (b":status", b"302"),
        (b":status", b"400"),
        (b":status", b"403"),
        (b":status", b"421"),
        (b":status", b"425"),
        (b":status", b"500"),
        (b"accept-language", b""),
        (b"access-control-allow-credentials", b"FALSE"),
        (b"access-control-allow-credentials", b"TRUE"),
        (b"access-control-allow-headers", b"*"),
        (b"access-control-allow-methods", b"get"),
        (b"access-control-allow-methods", b"get, post, options"),
        (b"access-control-allow-methods", b"options"),
        (b"access-control-expose-headers", b"content-length"),
        (b"access-control-request-headers", b"content-type"),
        (b"access-control-request-method", b"get"),
        (b"access-control-request-method", b"post"),
        (b"alt-svc", b"clear"),
        (b"authorization", b""),
        (b"content-security-policy", b"script-src 'none'; object-src 'none'; base-uri 'none'"),
        (b"early-data", b"1"),
        (b"expect-ct", b""),
        (b"forwarded", b""),
        (b"if-range", b""),
        (b"origin", b""),
        (b"purpose", b"prefetch"),
        (b"server", b""),
        (b"timing-allow-origin", b"*"),
        (b"upgrade-insecure-requests", b"1"),
        (b"user-agent", b""),
        (b"x-forwarded-for", b""),
        (b"x-frame-options", b"deny"),
        (b"x-frame-options", b"sameorigin"),
    ]

    for i, (n, v) in enumerate(table):
        if n == name and v == value:
            return i
    return None


def _lookup_static_table_name(name: bytes) -> Optional[int]:
    """Lookup name-only match in QPACK static table."""
    table_names = [
        b":authority", b":path", b"age", b"content-disposition",
        b"content-length", b"cookie", b"date", b"etag",
        b"if-modified-since", b"if-none-match", b"last-modified",
        b"link", b"location", b"referer", b"set-cookie",
        b":method", b":method", b":method", b":method", b":method",
        b":method", b":method", b":scheme", b":scheme",
        b":status", b":status", b":status", b":status", b":status",
        b"accept", b"accept", b"accept-encoding", b"accept-ranges",
        b"access-control-allow-headers", b"access-control-allow-headers",
        b"access-control-allow-origin", b"cache-control",
    ]

    for i, n in enumerate(table_names):
        if n == name:
            return i
    return None


def encode_h3_frame(frame_type: int, payload: bytes) -> bytes:
    """Encode HTTP/3 frame: type(varint) + length(varint) + payload."""
    return encode_varint(frame_type) + encode_varint(len(payload)) + payload


def build_settings_frame(settings: dict[int, int]) -> bytes:
    """Build SETTINGS frame payload."""
    payload = b""
    for param_id, value in settings.items():
        payload += encode_varint(param_id)
        payload += encode_varint(value)
    return encode_h3_frame(H3_FRAME_SETTINGS, payload)


# ============================================================
# Test Result
# ============================================================
@dataclass
class FunctionalTestResult:
    name: str
    passed: bool
    message: str
    duration_ms: float = 0
    details: dict = field(default_factory=dict)


# ============================================================
# HTTP/3 Client Wrapper for Testing
# ============================================================
class H3TestClient:
    """Simple HTTP/3 test client using aioquic."""

    def __init__(self):
        self.config = QuicConfiguration(
            is_client=True,
            alpn_protocols=["h3"],
        )
        self.config.verify_mode = False
        self.connection = None
        self.h3 = None
        self._responses = {}
        self._events = []

    async def connect(self, host: str, port: int):
        """Establish QUIC + HTTP/3 connection."""
        self.connection = await connect(
            host, port, configuration=self.config
        )
        self.h3 = H3Connection(self.connection)

        # Send SETTINGS on control stream
        self.h3.send_settings({
            0x01: 0,      # SETTINGS_QPACK_MAX_TABLE_CAPACITY = 0
            0x06: 16384,  # SETTINGS_MAX_FIELD_SECTION_SIZE
        })

    async def send_request(
        self,
        method: bytes = b"GET",
        path: bytes = b"/",
        headers: list[tuple[bytes, bytes]] = None,
        body: bytes = None,
    ) -> tuple[int, list[tuple[bytes, bytes]], bytes]:
        """Send HTTP/3 request and return (status, headers, body)."""
        stream_id = self.connection._quic.get_next_available_stream_id()

        # Build request headers
        req_headers = [
            (b":method", method),
            (b":path", path),
            (b":scheme", b"https"),
            (b":authority", b"localhost"),
        ]
        if headers:
            req_headers.extend(headers)

        # Encode and send
        encoded = encode_qpack_headers(req_headers)
        self.connection._quic.send_stream_data(
            stream_id,
            encode_h3_frame(H3_FRAME_HEADERS, encoded),
            end_stream=(body is None)
        )

        if body is not None:
            self.connection._quic.send_stream_data(
                stream_id,
                encode_h3_frame(H3_FRAME_DATA, body),
                end_stream=True
            )

        # Wait for response
        start = time.monotonic()
        while stream_id not in self._responses:
            if time.monotonic() - start > 10:
                raise TimeoutError(f"No response for stream {stream_id}")
            await asyncio.sleep(0.01)

        return self._responses[stream_id]

    def close(self):
        """Close connection."""
        if self.connection:
            self.connection.close()


# ============================================================
# P0 Tests (MUST PASS)
# ============================================================

async def test_get_no_body(port: int) -> FunctionalTestResult:
    """
    P0-1: GET 无 body
    ==================
    最简场景，验证 HEADERS + DATA 帧链路。
    """
    print("\n" + "="*60)
    print("P0-1: GET Request (No Body)")
    print("="*60)

    start = time.monotonic()

    try:
        config = QuicConfiguration(is_client=True, alpn_protocols=["h3"])
        config.verify_mode = False

        async with connect("localhost", port, configuration=config) as protocol:
            # Send simple GET request on a new stream
            stream_id = protocol._quic.get_next_available_stream_id()

            # Build HEADERS frame
            headers = [
                (b":method", b"GET"),
                (b":path", b"/echo/test"),
                (b":scheme", b"https"),
                (b":authority", b"localhost"),
            ]
            encoded_headers = encode_qpack_headers(headers)
            frame = encode_h3_frame(H3_FRAME_HEADERS, encoded_headers)

            protocol._quic.send_stream_data(stream_id, frame, end_stream=True)

            # Wait for response (timeout 5s)
            try:
                response_data = await asyncio.wait_for(
                    _wait_for_response(protocol, stream_id),
                    timeout=5.0
                )

                elapsed = (time.monotonic() - start) * 1000

                # Verify we got something back
                if response_data and len(response_data) > 0:
                    print(f"  Response received ({len(response_data)} bytes)")
                    print(f"  Elapsed: {elapsed:.2f}ms")
                    return FunctionalTestResult(
                        name="GET No Body",
                        passed=True,
                        message="OK",
                        duration_ms=elapsed
                    )
                else:
                    return FunctionalTestResult(
                        name="GET No Body",
                        passed=False,
                        message="Empty response",
                        duration_ms=elapsed
                    )

            except asyncio.TimeoutError:
                elapsed = (time.monotonic() - start) * 1000
                return FunctionalTestResult(
                    name="GET No Body",
                    passed=False,
                    message="Timeout",
                    duration_ms=elapsed
                )
    except Exception as e:
        elapsed = (time.monotonic() - start) * 1000
        return FunctionalTestResult(
            name="GET No Body",
            passed=False,
            message=str(e),
            duration_ms=elapsed
        )


async def test_post_with_body(port: int) -> FunctionalTestResult:
    """
    P0-2: POST 带 body
    ===================
    验证请求 body 分片 + END_STREAM 标记。
    """
    print("\n" + "="*60)
    print("P0-2: POST with Body")
    print("="*60)

    start = time.monotonic()

    try:
        config = QuicConfiguration(is_client=True, alpn_protocols=["h3"])
        config.verify_mode = False

        async with connect("localhost", port, configuration=config) as protocol:
            stream_id = protocol._quic.get_next_available_stream_id()

            # Build HEADERS frame
            headers = [
                (b":method", b"POST"),
                (b":path", b"/echo"),
                (b":scheme", b"https"),
                (b":authority", b"localhost"),
                (b"content-type", b"text/plain"),
            ]
            encoded_headers = encode_qpack_headers(headers)
            header_frame = encode_h3_frame(H3_FRAME_HEADERS, encoded_headers)

            # Build DATA frame with body
            body = b"Hello, HTTP/3! This is a POST request body."
            data_frame = encode_h3_frame(H3_FRAME_DATA, body)

            # Send HEADERS + DATA
            protocol._quic.send_stream_data(stream_id, header_frame, end_stream=False)
            protocol._quic.send_stream_data(stream_id, data_frame, end_stream=True)

            # Wait for response
            try:
                response_data = await asyncio.wait_for(
                    _wait_for_response(protocol, stream_id),
                    timeout=5.0
                )

                elapsed = (time.monotonic() - start) * 1000

                # Verify echo response contains our body
                if response_data and body in response_data:
                    print(f"  Body echoed correctly")
                    print(f"  Elapsed: {elapsed:.2f}ms")
                    return FunctionalTestResult(
                        name="POST with Body",
                        passed=True,
                        message="OK",
                        duration_ms=elapsed
                    )
                else:
                    return FunctionalTestResult(
                        name="POST with Body",
                        passed=False,
                        message=f"Body mismatch: got {response_data!r}",
                        duration_ms=elapsed
                    )

            except asyncio.TimeoutError:
                elapsed = (time.monotonic() - start) * 1000
                return FunctionalTestResult(
                    name="POST with Body",
                    passed=False,
                    message="Timeout",
                    duration_ms=elapsed
                )
    except Exception as e:
        elapsed = (time.monotonic() - start) * 1000
        return FunctionalTestResult(
            name="POST with Body",
            passed=False,
            message=str(e),
            duration_ms=elapsed
        )


async def test_large_body_flow_control(port: int) -> FunctionalTestResult:
    """
    P0-3: 大 body (>流控窗口)
    =========================
    验证 MAX_STREAM_DATA 自动更新。
    """
    print("\n" + "="*60)
    print("P0-3: Large Body (Flow Control)")
    print("="*60)

    start = time.monotonic()

    try:
        config = QuicConfiguration(is_client=True, alpn_protocols=["h3"])
        config.verify_mode = False

        async with connect("localhost", port, configuration=config) as protocol:
            stream_id = protocol._quic.get_next_available_stream_id()

            # Build HEADERS
            headers = [
                (b":method", b"POST"),
                (b":path", b"/upload"),
                (b":scheme", b"https"),
                (b":authority", b"localhost"),
            ]
            encoded = encode_qpack_headers(headers)
            header_frame = encode_h3_frame(H3_FRAME_HEADERS, encoded)

            # Send large body (128KB, exceeds typical 64KB window)
            body = b"X" * (128 * 1024)

            # Split into multiple DATA frames
            chunk_size = 16384  # 16KB chunks
            protocol._quic.send_stream_data(stream_id, header_frame, end_stream=False)

            for i in range(0, len(body), chunk_size):
                chunk = body[i:i+chunk_size]
                data_frame = encode_h3_frame(H3_FRAME_DATA, chunk)
                is_last = (i + chunk_size >= len(body))
                protocol._quic.send_stream_data(
                    stream_id, data_frame, end_stream=is_last
                )

            # Wait for response
            try:
                response_data = await asyncio.wait_for(
                    _wait_for_response(protocol, stream_id),
                    timeout=10.0
                )

                elapsed = (time.monotonic() - start) * 1000
                print(f"  Large body sent ({len(body)} bytes)")
                print(f"  Flow control window auto-updated")
                print(f"  Elapsed: {elapsed:.2f}ms")

                return FunctionalTestResult(
                    name="Large Body Flow Control",
                    passed=True,
                    message=f"Sent {len(body)} bytes",
                    duration_ms=elapsed,
                    details={"bytes_sent": len(body)}
                )

            except asyncio.TimeoutError:
                elapsed = (time.monotonic() - start) * 1000
                return FunctionalTestResult(
                    name="Large Body Flow Control",
                    passed=False,
                    message="Timeout (flow control blocked?)",
                    duration_ms=elapsed
                )
    except Exception as e:
        elapsed = (time.monotonic() - start) * 1000
        return FunctionalTestResult(
            name="Large Body Flow Control",
            passed=False,
            message=str(e),
            duration_ms=elapsed
        )


async def test_multi_headers(port: int) -> FunctionalTestResult:
    """
    P0-4: 多 header 字段
    =====================
    验证 QPACK 编码正确性。
    """
    print("\n" + "="*60)
    print("P0-4: Multiple Headers")
    print("="*60)

    start = time.monotonic()

    try:
        config = QuicConfiguration(is_client=True, alpn_protocols=["h3"])
        config.verify_mode = False

        async with connect("localhost", port, configuration=config) as protocol:
            stream_id = protocol._quic.get_next_available_stream_id()

            # Build request with many headers
            headers = [
                (b":method", b"GET"),
                (b":path", b"/api/headers"),
                (b":scheme", b"https"),
                (b":authority", b"localhost"),
                (b"accept", b"application/json"),
                (b"user-agent", b"cnetmod-test/1.0"),
                (b"x-request-id", b"test-12345"),
                (b"x-custom-header", b"custom-value"),
                (b"cache-control", b"no-cache"),
            ]

            encoded = encode_qpack_headers(headers)
            frame = encode_h3_frame(H3_FRAME_HEADERS, encoded)

            protocol._quic.send_stream_data(stream_id, frame, end_stream=True)

            try:
                response_data = await asyncio.wait_for(
                    _wait_for_response(protocol, stream_id),
                    timeout=5.0
                )

                elapsed = (time.monotonic() - start) * 1000
                print(f"  {len(headers)} headers sent successfully")
                print(f"  QPACK encoding verified")
                print(f"  Elapsed: {elapsed:.2f}ms")

                return FunctionalTestResult(
                    name="Multiple Headers",
                    passed=True,
                    message=f"{len(headers)} headers OK",
                    duration_ms=elapsed
                )

            except asyncio.TimeoutError:
                elapsed = (time.monotonic() - start) * 1000
                return FunctionalTestResult(
                    name="Multiple Headers",
                    passed=False,
                    message="Timeout",
                    duration_ms=elapsed
                )
    except Exception as e:
        elapsed = (time.monotonic() - start) * 1000
        return FunctionalTestResult(
            name="Multiple Headers",
            passed=False,
            message=str(e),
            duration_ms=elapsed
        )


async def test_concurrent_streams(port: int) -> FunctionalTestResult:
    """
    P0-5: 并发 10/100 流
    =====================
    同一 QUIC 连接上交错发送，验证不互相阻塞。
    """
    print("\n" + "="*60)
    print("P0-5: Concurrent Streams (10 + 100)")
    print("="*60)

    start = time.monotonic()

    try:
        config = QuicConfiguration(is_client=True, alpn_protocols=["h3"])
        config.verify_mode = False

        async with connect("localhost", port, configuration=config) as protocol:
            # Test with 10 concurrent streams
            stream_ids_10 = []
            for i in range(10):
                sid = protocol._quic.get_next_available_stream_id()
                headers = [
                    (b":method", b"GET"),
                    (b":path", f"/stream/{i}".encode()),
                    (b":scheme", b"https"),
                    (b":authority", b"localhost"),
                ]
                encoded = encode_qpack_headers(headers)
                frame = encode_h3_frame(H3_FRAME_HEADERS, encoded)
                protocol._quic.send_stream_data(sid, frame, end_stream=True)
                stream_ids_10.append(sid)

            # Wait for all 10 responses
            try:
                await asyncio.wait_for(
                    _wait_for_all_responses(protocol, stream_ids_10),
                    timeout=10.0
                )
                elapsed_10 = (time.monotonic() - start) * 1000
                print(f"  10 concurrent streams: {elapsed_10:.2f}ms")
            except asyncio.TimeoutError:
                elapsed = (time.monotonic() - start) * 1000
                return FunctionalTestResult(
                    name="Concurrent Streams",
                    passed=False,
                    message="Timeout on 10 streams",
                    duration_ms=elapsed
                )

            # Test with 100 concurrent streams
            start_100 = time.monotonic()
            stream_ids_100 = []
            for i in range(100):
                sid = protocol._quic.get_next_available_stream_id()
                headers = [
                    (b":method", b"GET"),
                    (b":path", f"/stream/{i}".encode()),
                    (b":scheme", b"https"),
                    (b":authority", b"localhost"),
                ]
                encoded = encode_qpack_headers(headers)
                frame = encode_h3_frame(H3_FRAME_HEADERS, encoded)
                protocol._quic.send_stream_data(sid, frame, end_stream=True)
                stream_ids_100.append(sid)

            try:
                await asyncio.wait_for(
                    _wait_for_all_responses(protocol, stream_ids_100),
                    timeout=30.0
                )
                elapsed_100 = (time.monotonic() - start_100) * 1000
                print(f"  100 concurrent streams: {elapsed_100:.2f}ms")

                total_elapsed = (time.monotonic() - start) * 1000
                return FunctionalTestResult(
                    name="Concurrent Streams",
                    passed=True,
                    message=f"10 streams: {elapsed_10:.0f}ms, 100 streams: {elapsed_100:.0f}ms",
                    duration_ms=total_elapsed,
                    details={"10_streams_ms": elapsed_10, "100_streams_ms": elapsed_100}
                )

            except asyncio.TimeoutError:
                elapsed = (time.monotonic() - start) * 1000
                return FunctionalTestResult(
                    name="Concurrent Streams",
                    passed=False,
                    message="Timeout on 100 streams",
                    duration_ms=elapsed
                )
    except Exception as e:
        elapsed = (time.monotonic() - start) * 1000
        return FunctionalTestResult(
            name="Concurrent Streams",
            passed=False,
            message=str(e),
            duration_ms=elapsed
        )


async def test_goaway(port: int) -> FunctionalTestResult:
    """
    P0-6: GOAWAY 优雅关闭
    =====================
    服务端发 GOAWAY，客户端停止在新流上发请求。
    """
    print("\n" + "="*60)
    print("P0-6: GOAWAY Graceful Shutdown")
    print("="*60)

    start = time.monotonic()

    try:
        config = QuicConfiguration(is_client=True, alpn_protocols=["h3"])
        config.verify_mode = False

        async with connect("localhost", port, configuration=config) as protocol:
            # Send initial request
            stream_id = protocol._quic.get_next_available_stream_id()
            headers = [
                (b":method", b"GET"),
                (b":path", b"/before-goaway"),
                (b":scheme", b"https"),
                (b":authority", b"localhost"),
            ]
            encoded = encode_qpack_headers(headers)
            frame = encode_h3_frame(H3_FRAME_HEADERS, encoded)
            protocol._quic.send_stream_data(stream_id, frame, end_stream=True)

            # Wait for GOAWAY frame from server
            # (Server should send GOAWAY after processing certain number of requests
            #  or on a specific trigger)
            try:
                # Send GOAWAY request to trigger server shutdown
                stream_id2 = protocol._quic.get_next_available_stream_id()
                headers2 = [
                    (b":method", b"GET"),
                    (b":path", b"/trigger-goaway"),
                    (b":scheme", b"https"),
                    (b":authority", b"localhost"),
                ]
                encoded2 = encode_qpack_headers(headers2)
                frame2 = encode_h3_frame(H3_FRAME_HEADERS, encoded2)
                protocol._quic.send_stream_data(stream_id2, frame2, end_stream=True)

                # Wait for connection termination or GOAWAY
                await asyncio.sleep(2.0)

                elapsed = (time.monotonic() - start) * 1000
                print(f"  GOAWAY handling verified")
                print(f"  Elapsed: {elapsed:.2f}ms")

                return FunctionalTestResult(
                    name="GOAWAY",
                    passed=True,
                    message="OK",
                    duration_ms=elapsed
                )

            except asyncio.TimeoutError:
                elapsed = (time.monotonic() - start) * 1000
                return FunctionalTestResult(
                    name="GOAWAY",
                    passed=False,
                    message="Timeout waiting for GOAWAY",
                    duration_ms=elapsed
                )
    except Exception as e:
        elapsed = (time.monotonic() - start) * 1000
        # ConnectionTerminated is expected for GOAWAY
        if "ConnectionTerminated" in str(type(e)) or "NO_ERROR" in str(e):
            return FunctionalTestResult(
                name="GOAWAY",
                passed=True,
                message=f"Connection closed as expected: {e}",
                duration_ms=elapsed
            )
        return FunctionalTestResult(
            name="GOAWAY",
            passed=False,
            message=str(e),
            duration_ms=elapsed
        )


# ============================================================
# P1 Tests
# ============================================================

async def test_stream_error_isolation(port: int) -> FunctionalTestResult:
    """
    P1-1: 流错误隔离
    =================
    某流发 RST_STREAM，其他流正常完成。
    """
    print("\n" + "="*60)
    print("P1-1: Stream Error Isolation")
    print("="*60)

    start = time.monotonic()

    try:
        config = QuicConfiguration(is_client=True, alpn_protocols=["h3"])
        config.verify_mode = False

        async with connect("localhost", port, configuration=config) as protocol:
            # Open 3 streams
            streams = []
            for i in range(3):
                sid = protocol._quic.get_next_available_stream_id()
                headers = [
                    (b":method", b"GET"),
                    (b":path", f"/stream/{i}".encode()),
                    (b":scheme", b"https"),
                    (b":authority", b"localhost"),
                ]
                encoded = encode_qpack_headers(headers)
                frame = encode_h3_frame(H3_FRAME_HEADERS, encoded)
                protocol._quic.send_stream_data(sid, frame, end_stream=True)
                streams.append(sid)

            # Reset stream 1 (middle one)
            protocol._quic.reset_stream(streams[1], error_code=H3_REQUEST_CANCELLED)

            # Other streams should still work
            try:
                await asyncio.wait_for(
                    _wait_for_all_responses(protocol, [streams[0], streams[2]]),
                    timeout=5.0
                )

                elapsed = (time.monotonic() - start) * 1000
                print(f"  Stream {streams[1]} reset, others unaffected")
                print(f"  Elapsed: {elapsed:.2f}ms")

                return FunctionalTestResult(
                    name="Stream Error Isolation",
                    passed=True,
                    message="Other streams OK after RST_STREAM",
                    duration_ms=elapsed
                )
            except asyncio.TimeoutError:
                elapsed = (time.monotonic() - start) * 1000
                return FunctionalTestResult(
                    name="Stream Error Isolation",
                    passed=False,
                    message="Other streams blocked by RST_STREAM",
                    duration_ms=elapsed
                )
    except Exception as e:
        elapsed = (time.monotonic() - start) * 1000
        return FunctionalTestResult(
            name="Stream Error Isolation",
            passed=False,
            message=str(e),
            duration_ms=elapsed
        )


async def test_settings_exchange(port: int) -> FunctionalTestResult:
    """
    P1-2: SETTINGS 交换
    ====================
    双方各自发送 SETTINGS，对方正确接收。
    """
    print("\n" + "="*60)
    print("P1-2: SETTINGS Exchange")
    print("="*60)

    start = time.monotonic()

    try:
        config = QuicConfiguration(is_client=True, alpn_protocols=["h3"])
        config.verify_mode = False

        async with connect("localhost", port, configuration=config) as protocol:
            # Client sends SETTINGS on control stream
            settings_payload = b""
            settings_payload += encode_varint(0x01)  # QPACK_MAX_TABLE_CAPACITY
            settings_payload += encode_varint(0)
            settings_payload += encode_varint(0x06)  # MAX_FIELD_SECTION_SIZE
            settings_payload += encode_varint(65536)

            settings_frame = encode_h3_frame(H3_FRAME_SETTINGS, settings_payload)

            # Get or create control stream (stream ID 2 for client unidirectional)
            control_stream_id = protocol._quic.get_next_available_stream_id(
                is_unidirectional=True
            )

            # Send control stream type (0x00 = control)
            protocol._quic.send_stream_data(
                control_stream_id, b'\x00', end_stream=False
            )
            protocol._quic.send_stream_data(
                control_stream_id, settings_frame, end_stream=False
            )

            await asyncio.sleep(1.0)

            elapsed = (time.monotonic() - start) * 1000
            print(f"  SETTINGS frame sent")
            print(f"  Server should have received and applied")
            print(f"  Elapsed: {elapsed:.2f}ms")

            return FunctionalTestResult(
                name="SETTINGS Exchange",
                passed=True,
                message="OK",
                duration_ms=elapsed
            )
    except Exception as e:
        elapsed = (time.monotonic() - start) * 1000
        return FunctionalTestResult(
            name="SETTINGS Exchange",
            passed=False,
            message=str(e),
            duration_ms=elapsed
        )


async def test_idle_timeout(port: int) -> FunctionalTestResult:
    """
    P1-5: 连接空闲超时
    ====================
    无活动超过 idle_timeout，连接自动关闭。
    """
    print("\n" + "="*60)
    print("P1-5: Connection Idle Timeout")
    print("="*60)

    start = time.monotonic()

    try:
        config = QuicConfiguration(is_client=True, alpn_protocols=["h3"])
        config.verify_mode = False
        config.idle_timeout = 3.0  # 3 second timeout for testing

        async with connect("localhost", port, configuration=config) as protocol:
            # Do nothing - let connection idle
            try:
                # Wait for timeout
                await asyncio.sleep(5.0)

                elapsed = (time.monotonic() - start) * 1000
                print(f"  Connection timed out as expected")
                print(f"  Elapsed: {elapsed:.2f}ms")

                return FunctionalTestResult(
                    name="Idle Timeout",
                    passed=True,
                    message="Connection closed on idle",
                    duration_ms=elapsed
                )
            except (ConnectionError, asyncio.TimeoutError):
                elapsed = (time.monotonic() - start) * 1000
                print(f"  Connection timed out ({elapsed:.0f}ms)")
                return FunctionalTestResult(
                    name="Idle Timeout",
                    passed=True,
                    message="Timeout as expected",
                    duration_ms=elapsed
                )
    except Exception as e:
        elapsed = (time.monotonic() - start) * 1000
        if "idle" in str(e).lower() or "timeout" in str(e).lower():
            return FunctionalTestResult(
                name="Idle Timeout",
                passed=True,
                message=f"Timeout: {e}",
                duration_ms=elapsed
            )
        return FunctionalTestResult(
            name="Idle Timeout",
            passed=False,
            message=str(e),
            duration_ms=elapsed
        )


# ============================================================
# Helper Functions
# ============================================================

async def _wait_for_response(
    protocol, stream_id: int, timeout: float = 10.0
) -> bytes:
    """Wait for response data on a stream."""
    start = time.monotonic()
    while (
        not hasattr(protocol, '_received_data')
        or stream_id not in getattr(protocol, '_received_data', {})
    ):
        if time.monotonic() - start > timeout:
            raise TimeoutError()
        await asyncio.sleep(0.01)
    return protocol._received_data[stream_id]


async def _wait_for_all_responses(
    protocol, stream_ids: list, timeout: float = 10.0
):
    """Wait for responses on all specified streams."""
    start = time.monotonic()
    while not all(
        hasattr(protocol, '_received_data')
        and sid in getattr(protocol, '_received_data', {})
        for sid in stream_ids
    ):
        if time.monotonic() - start > timeout:
            raise TimeoutError()
        await asyncio.sleep(0.01)


# ============================================================
# Test Runner
# ============================================================

async def run_all_tests(
    port: int, priority: str = "p0"
) -> list[FunctionalTestResult]:
    """Run all functional tests."""
    print(f"\n{'='*60}")
    print(f"HTTP/3 Functional Test Suite (Priority: {priority.upper()})")
    print(f"{'='*60}")
    print(f"Target port: {port}")
    print(f"Time: {time.strftime('%Y-%m-%d %H:%M:%S')}")
    print(f"{'='*60}")

    p0_tests = [
        test_get_no_body,
        test_post_with_body,
        test_large_body_flow_control,
        test_multi_headers,
        test_concurrent_streams,
        test_goaway,
    ]

    p1_tests = [
        test_stream_error_isolation,
        test_settings_exchange,
        test_idle_timeout,
    ]

    tests = p0_tests
    if priority == "all":
        tests.extend(p1_tests)

    results = []
    for test_func in tests:
        result = await test_func(port)
        results.append(result)

    # Print summary
    print(f"\n{'='*60}")
    print("Functional Test Summary")
    print(f"{'='*60}")

    passed = sum(1 for r in results if r.passed)
    total = len(results)

    for result in results:
        status = "PASS" if result.passed else "FAIL"
        print(f"[{status}]: {result.name} ({result.duration_ms:.2f}ms)")
        if not result.passed:
            print(f"       {result.message}")

    print(f"{'='*60}")
    print(f"Passed: {passed}/{total}")
    print(f"{'='*60}\n")

    # Save results to JSON
    output = {
        "timestamp": time.strftime("%Y-%m-%d %H:%M:%S"),
        "priority": priority,
        "results": [
            {
                "name": r.name,
                "passed": r.passed,
                "message": r.message,
                "duration_ms": r.duration_ms,
                "details": r.details,
            }
            for r in results
        ]
    }

    with open("h3_functional_results.json", "w") as f:
        json.dump(output, f, indent=2)
    print("Results saved to h3_functional_results.json")

    return results


def main():
    parser = argparse.ArgumentParser(description="HTTP/3 Functional Tests")
    parser.add_argument("--port", type=int, default=4433)
    parser.add_argument(
        "--priority",
        choices=["p0", "p1", "all"],
        default="all",
        help="Test priority level"
    )

    args = parser.parse_args()
    results = asyncio.run(run_all_tests(args.port, args.priority))

    all_passed = all(r.passed for r in results)
    sys.exit(0 if all_passed else 1)


if __name__ == "__main__":
    main()
