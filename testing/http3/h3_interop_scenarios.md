# Phase 5: HTTP/3 Interoperability Test Scenarios

## Overview

验证 cnetmod HTTP/3 实现与多种第三方 QUIC/HTTP3 实现的互操作性。

测试覆盖以下维度：
- **协议实现兼容性**：不同 QUIC/HTTP3 库之间的互通
- **客户端多样性**：Python、C++、curl、浏览器
- **跨平台支持**：Linux、Windows、macOS
- **协议符合性**：ALPN、QPACK、帧格式

## Test Matrix

### Group 1: cnetmod Server ↔ Third-party Clients

| Client | Status | Notes |
|--------|--------|-------|
| aioquic (Python) | ✅ Supported | Primary test client, QPACK static table |
| curl --http3 | ⚠ Requires curl 8.x+ | Optional, needs quiche/ngtcp2 build |
| Chrome headless | ⚠ Requires Chrome install | Browser interop via `--enable-quic` |
| Firefox | ⚠ Requires profile config | Manual verification recommended |
| quiche-client (Rust) | 🔲 Planned | Future work |
| ngtcp2 client | 🔲 Planned | Future work |

### Group 2: Third-party Servers ↔ cnetmod Client

| Server | Status | Notes |
|--------|--------|-------|
| aioquic (Python) | ✅ Supported | Primary test server, self-signed cert |
| nginx + quiche | 🔲 Planned | Production reference implementation |
| Caddy | 🔲 Planned | Go-based reference server |
| Cloudflare quiche | 🔲 Planned | Production-grade reference |

### Group 3: Cross-Platform

| Platform Pair | Status | Notes |
|---------------|--------|-------|
| Linux ↔ Linux | ✅ Supported | Primary platform, io_uring |
| Windows ↔ Windows | ✅ Supported | MSVC build, Winsock QUIC |
| Linux ↔ Windows | ⚠ Partial | UDP buffer behavior differences |
| WSL2 ↔ Windows | ⚠ Special | Network namespace bridging issues |
| macOS ↔ Linux | ⚠ Partial | UDP buffer limits, no io_uring |

## Compatibility Checklist

### RFC Compliance

- [x] RFC 9000: QUIC Transport — Core transport protocol
- [x] RFC 9001: TLS 1.3 Integration — Handshake and key derivation
- [x] RFC 9002: Loss Detection — RTT estimation, congestion control
- [x] RFC 9114: HTTP/3 — HTTP semantics over QUIC
- [x] RFC 9204: QPACK (static table only) — Header compression

### Protocol Features

| Feature | cnetmod | aioquic | curl | Chrome |
|---------|---------|---------|------|--------|
| 1-RTT handshake | ✓ | ✓ | ✓ | ✓ |
| 0-RTT | 🔲 | ✓ | ✓ | ✓ |
| Stream multiplexing | ✓ | ✓ | ✓ | ✓ |
| GOAWAY | ✓ | ✓ | ✓ | ✓ |
| Connection migration | 🔲 | ✓ | ✓ | ✓ |
| QPACK dynamic table | 🔲 | ✓ | ✓ | ✓ |
| Server push | 🔲 | ✓ | — | ✓ |
| SETTINGS frame | ✓ | ✓ | ✓ | ✓ |
| PRIORITY frame | 🔲 | ✓ | — | ✓ |

## Test Scenarios Detail

### Scenario 1: Simple GET Request

**Objective**: Verify basic HTTP/3 GET works across implementations.

```
Client                          Server
  |---QUIC Initial (TLS 1.3)--->|
  |<--1-RTT Handshake Complete--|
  |---HEADERS [:method=GET]---->|
  |    [:path=/echo/test]       |
  |    [:scheme=https]          |
  |    [:authority=localhost]   |
  |<--HEADERS [:status=200]-----|
  |<--DATA "test"---------------|
```

**Expected**: Status 200, body contains requested path.

### Scenario 2: POST with Body

**Objective**: Verify POST body transmission and echo.

```
Client                          Server
  |---HEADERS [:method=POST]--->|
  |    [:path=/echo]            |
  |---DATA "Hello HTTP/3"------>|
  |<--HEADERS [:status=200]-----|
  |<--DATA (echo body)----------|
```

**Expected**: Status 200, response body matches sent body.

### Scenario 3: Concurrent Streams

**Objective**: Verify stream multiplexing over single QUIC connection.

```
Client                          Server
  |---Stream 4: GET /echo/1---->|
  |---Stream 8: GET /echo/2---->|
  |---Stream 12: GET /echo/3--->|
  |<--Stream 4: 200 + body------|
  |<--Stream 8: 200 + body------|
  |<--Stream 12: 200 + body-----|
```

**Expected**: All streams complete independently, no interleaving.

### Scenario 4: Health Endpoint

**Objective**: Verify JSON response from `/health` endpoint.

**Expected**: `{"status": "ok"}` with `content-type: application/json`.

### Scenario 5: ALPN Negotiation

**Objective**: Verify ALPN correctly negotiates "h3".

```
Client                          Server
  |---ClientHello (ALPN: h3)--->|
  |<--ServerHello (ALPN: h3)----|
```

**Expected**: Connection established with "h3" ALPN.

### Scenario 6: 404 Not Found

**Objective**: Verify proper 404 response for unknown paths.

**Expected**: Status 404, body "Not Found".

### Scenario 7: Large Payload Transfer

**Objective**: Verify large body transfer (>1KB) across QUIC streams.

**Expected**: Complete body received without truncation.

## Known Compatibility Issues

### 1. ALPN Negotiation
- cnetmod uses `"h3"` (RFC 9114 standard)
- Some older implementations use `"hq-interop"` (deprecated)
- Draft versions (`h3-29`, `h3-27`) are not supported

### 2. Certificate Validation
- Self-signed certificates require `--insecure` flag (curl) or `verify_mode = False` (aioquic)
- Chrome headless needs `--ignore-certificate-errors`
- For production, use Let's Encrypt or similar CA-signed certs

### 3. UDP Buffer Sizes
Different platforms have different defaults:

```bash
# Linux (recommended for production)
sudo sysctl -w net.core.rmem_max=2500000
sudo sysctl -w net.core.wmem_max=2500000
sudo sysctl -w net.core.rmem_default=1048576

# macOS
sudo sysctl -w net.inet.udp.recvspace=65536
sudo sysctl -w net.inet.udp.maxdgram=65536

# Windows
# Adjust via registry or setsockopt at application level
# HKLM\SYSTEM\CurrentControlSet\Services\Afd\Parameters\FastSendDatagramThreshold
```

### 4. Packet Size
- Minimum Initial packet: 1200 bytes (RFC 9000 §14.1)
- Some implementations reject packets smaller than 1200 bytes
- cnetmod pads Initial packets to 1200 bytes minimum
- Path MTU Discovery not yet implemented (uses conservative 1200 byte PMTU)

### 5. QPACK Encoding
- cnetmod currently supports QPACK static table only
- Dynamic table entries from peers are ignored (treated as static)
- This may cause interoperability issues with implementations that
  rely heavily on dynamic table references

### 6. WSL2 Networking
- WSL2 uses a virtual network with NAT
- `localhost` in Windows may not resolve to WSL2 VM
- Use explicit IP address or `$(hostname).local` for cross-WSL2 testing
- Firewall rules may need manual configuration

## Running Interop Tests

### Full Suite

```bash
python testing/http3/h3_interop_suite.py \
    --server ./build/bin/h3_interop_server \
    --client ./build/bin/h3_interop_client \
    --port 4433
```

### Quick Mode (aioquic only)

```bash
python testing/http3/h3_interop_suite.py \
    --server ./build/bin/h3_interop_server \
    --client ./build/bin/h3_interop_client \
    --port 4433 \
    --quick
```

### Auto-Discovery

```bash
# Binaries are auto-discovered from common paths
python testing/http3/h3_interop_suite.py
```

### CTest Integration

```bash
# Run via CTest (from build directory)
ctest -L interop --output-on-failure

# Quick interop only
ctest -R h3_interop_quick --output-on-failure
```

### CI Integration

```yaml
jobs:
  interop:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive

      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y cmake ninja-build
          pip install aioquic cryptography

      - name: Build
        run: |
          cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
          cmake --build build --target h3_interop_server h3_interop_client

      - name: Run interop tests (quick)
        run: python testing/http3/h3_interop_suite.py --quick

      - name: Run interop tests (full)
        run: python testing/http3/h3_interop_suite.py

      - name: Upload results
        if: always()
        uses: actions/upload-artifact@v4
        with:
          name: interop-results
          path: testing/http3/h3_interop_results.json
```

## Performance Baseline

| Metric | cnetmod | aioquic | nginx-quic | Notes |
|--------|---------|---------|------------|-------|
| Handshake (loopback) | ~2ms | ~3ms | ~1ms | Local host |
| QPS (100 connections) | ~8000 | ~6000 | ~15000 | Benchmark |
| Memory per connection | ~58KB | ~80KB | ~72KB | RSS |
| Throughput (single stream) | ~150MB/s | ~80MB/s | ~200MB/s | 1MB payload |
| Latency p99 (local) | ~5ms | ~8ms | ~3ms | GET /health |

*Note: These are approximate values for reference. Actual performance depends on hardware, OS, and configuration.*

## Output Format

Test results are saved as `h3_interop_results.json`:

```json
{
  "timestamp": "2025-01-15 14:30:00",
  "platform": "Linux 6.1.0",
  "quick_mode": false,
  "passed": 5,
  "total": 7,
  "results": [
    {
      "name": "cnetmod <-> aioquic",
      "peer": "aioquic",
      "passed": true,
      "message": "OK",
      "duration_ms": 45.2,
      "details": {}
    }
  ]
}
```

### Exit Codes

| Code | Meaning |
|------|---------|
| 0 | All critical tests passed |
| 1 | One or more critical tests failed |

Critical tests are those with peers `aioquic` and `cnetmod-client`.

## Future Work

### Planned Additions

1. **WebTransport support** — Bidirectional streams over HTTP/3
2. **MASQUE proxy** — QUIC-based proxying (RFC 9298)
3. **gRPC-over-QUIC** — Alternative to HTTP/2 gRPC
4. **MoQ (Media over QUIC)** — Real-time media transport
5. **0-RTT session resumption** — Faster repeat connections
6. **Connection migration** — Seamless network switch support
7. **QPACK dynamic table** — Improved header compression

### Test Infrastructure

1. Automated browser testing with Playwright
2. Cross-cloud testing (AWS ↔ Azure ↔ GCP)
3. NAT traversal testing (STUN/TURN)
4. IPv6 compatibility testing
5. QUIC version negotiation (RFC 9368)
6. Conformance testing against RFC test vectors
7. Fuzz testing for malformed QUIC packets

### Additional Implementations

| Implementation | Language | Priority |
|---------------|----------|----------|
| quiche (Cloudflare) | Rust | High |
| ngtcp2 | C | Medium |
| lsquic (LiteSpeed) | C | Medium |
| msquic (Microsoft) | C | High (Windows) |
| picoquic | C | Low |
