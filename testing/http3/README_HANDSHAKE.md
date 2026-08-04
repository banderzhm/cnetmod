# QUIC Handshake Verification Guide

## 概述

本工具在 HTTP/3 应用层实现**之前**，验证 cnetmod QUIC 传输层的
TLS 1.3 握手链路是否正常工作。提供早期反馈，避免等到 HTTP/3 层
全部完成才发现问题。

## 前置条件

1. **BoringSSL** 已安装且启用了 QUIC API 支持
2. **自签名证书**已生成：
   ```bash
   openssl req -x509 -newkey rsa:2048 -nodes \
       -keyout server.key -out server.crt \
       -days 365 -subj "/CN=localhost"
   ```
3. **Python aioquic** 已安装：
   ```bash
   pip install aioquic>=1.0.0
   ```

## 构建

```bash
cmake -DCMAKE_BUILD_TYPE=Release \
      -DCNETMOD_ENABLE_QUIC=ON \
      -DCNETMOD_ENABLE_BORINGSSL_QUIC=ON \
      -S . -B build

cmake --build build --target quic_echo_server
```

## 运行

### Step 1: 启动 echo server

```bash
./build/bin/quic_echo_server --port 4433
```

预期输出：
```
[INFO] QUIC echo server initialized on port 4433
[INFO] QUIC echo server started listening on port 4433
QUIC echo server running on port 4433
Press Ctrl+C to stop
```

### Step 2: 运行握手测试脚本

```bash
python testing/http3/quic_handshake_test.py localhost 4433
```

预期输出：
```
============================================================
QUIC Handshake Verification Test
============================================================
Target: localhost:4433
Time: 2026-08-03 10:00:00
============================================================

Connecting to localhost:4433...
✓ QUIC connection initiated
✓ TLS 1.3 handshake completed (1.23ms)
✓ First stream data received (1.45ms)
  Data: 515549435f48414e445348414b455f54455354
✓ Echo verification successful (1.67ms)

============================================================
ALL VERIFICATIONS PASSED
============================================================
Handshake latency: 1.23ms
First data latency: 1.45ms
Total latency: 1.67ms
============================================================
```

## Success Criteria

| 检查项 | 说明 |
|--------|------|
| TLS 1.3 握手完成 | ClientHello → ServerHello → Finished 全链路 |
| QUIC 连接建立 | DCID 交换成功，传输参数协商通过 |
| 应用数据往返 | Echo 数据与发送数据一致 |
| 延迟合理 | 本地回环 < 10ms |

## Troubleshooting

### "handshake failed"
- 检查证书有效性（是否过期）
- 确认 ALPN 协议 `"h3"` 在客户端/服务端一致
- 确认 BoringSSL 版本支持 QUIC API

### "no data received"
- 检查防火墙是否放通 UDP 4433 端口
- 确认服务端正在运行且监听中
- 查看服务端日志中的连接接受错误

### "echo verification failed"
- 服务端可能未正确回显
- 检查流数据处理逻辑
- 确认流控窗口未耗尽
