# Windows HTTP/3 CPU 热点报告（2026-08-10）

## 采集条件

- Trace：`windows-h3-cpu-20260810-113949.etl`
- 进程：`h3_interop_server.exe`（PID 26008）
- 构建：`RelWithDebInfo`，已使用对应 PDB 解析 cnetmod 符号。
- 工具：Windows Performance Toolkit `xperf`。

下表是 **已解析的 h3_interop_server 用户态直接采样** 中的占比（共
9,446,812 weight），不是整台机器的 CPU 占比，也不是火焰图的 inclusive
调用树占比。它适合比较每次同口径测试的函数级变化。

## 前台热点

| 排名 | 函数 | 直接采样占比 |
| ---: | --- | ---: |
| 1 | `http::v2::huffman_decode` | 3.67% |
| 2 | `http::v3::http3_server_session::service_peer_stream` | 3.64% |
| 3 | `quic_connection::process_stream_frame` | 2.59% |
| 4 | `quic_connection::async_recv` | 1.73% |
| 5 | QPACK 解码状态的 `flat_map::operator[]` | 1.38% |
| 6 | `quic_connection::process_packet` | 1.36% |
| 7 | `poly1305_update` | 1.33% |
| 8 | `http::v3::detail::read_int` | 1.17% |
| 9 | `quic_connection::flush_send_queue` | 1.13% |
| 10 | `quic_connection::async_send` | 1.08% |
| 11 | `http::v3::qpack_decoder::decode` | 0.96% |
| 12 | stream-write completion MPMC queue `try_dequeue` | 0.94% |
| 13 | `async_mutex::lock_awaitable::await_ready` | 0.94% |
| 14 | `http3_request` 构造 | 0.91% |
| 15 | `quic_stream::receive` | 0.89% |

## 结论

1. **当前不是单一锁热点。** `async_mutex` 直接采样仅 0.94%，不能通过删除
   串行域或改回无保护并发来换取明显收益；那会破坏 QUIC 状态所有权语义。
2. **最值得优化的是 QPACK 的 Huffman 解码。** QPACK 按 RFC 9204 复用 HPACK
   的 Huffman 编码，因此符号显示为 `http::v2::huffman_decode` 是正确的，并不
   表示 H3 请求误走了 HTTP/2。它是当前第一名直接热点，应从解码表布局、快速
   路径和避免中间字符串分配入手，并以 QPACK 互操作测试保证语义不变。
3. **协议主路径占比合理但仍有可优化空间。** `service_peer_stream`、
   `process_stream_frame`、`process_packet`、QPACK 解码和 varint 读取合计明显，
   后续优化应以减少每帧对象构造、header 状态查找、重复解析和小对象分配为主。
4. **加密并非唯一瓶颈。** Poly1305 约 1.33%，低于头部/stream 处理；优先优化
   协议与数据结构热路径，而不是为了微小收益牺牲 TLS/QUIC 安全性。
5. **队列与调度可继续量化。** `drain_post_queue` 在直接采样中约 0.51%，但在
   调用树的 inclusive 视图中曾显示 5.74%；这说明它主要承担下游工作，不能把
   inclusive 时间误判为队列自身的原子操作开销。

## 可复现命令

```powershell
$env:_NT_SYMBOL_PATH = 'E:\github\cnetmod\cmake-build-quic-windows\bin\RelWithDebInfo;E:\github\cnetmod\cmake-build-quic-windows\bin\Debug'
$env:_NT_SYMCACHE_PATH = 'E:\github\cnetmod\windows-h3-cpu-20260810-113949.etl.NGENPDB'
& 'C:\Program Files (x86)\Windows Kits\10\Windows Performance Toolkit\xperf.exe' `
  -i 'E:\github\cnetmod\windows-h3-cpu-20260810-113949.etl' `
  -quiet -symbols -a profile -detail
```

对于持续自动化，应将此输出以 `wpaexporter` 的 CPU Usage (Sampled) profile
导出为 CSV，并和 `oha` 的吞吐、延迟、成功率 JSON 一起归档。
