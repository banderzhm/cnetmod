# HttpArena 头部引擎源码对照分析

- 日期：2026-10-21
- 目的：弄清榜首引擎凭什么用更少的 CPU 跑出更高的吞吐，找出 cnetmod 可复用的机制
- 源码位置：`E:\github\httparena-upstream\{zix,agensio,ioxide,vanilla,fib,cardigan}`（浅克隆）
- 榜单数据：`http-arena.com/data.json`（2026-10-21 抓取）
- cnetmod 基线：`develop` @ `e0361544`

## 0. 当前引擎联赛位置（每列第一名 = 1000 分）

| 家族 | 计分列 | cnetmod | 榜首 | 排名 |
|---|---:|---:|---:|---:|
| gRPC | 4 | 4213 | zix 3938 | **1 / 59** |
| HTTP/2 | 7 | 4577 | zix 6794 | 3 / 59 |
| HTTP/3 | 2 | 848 | zix 2000 | 3 / 59 |
| WebSocket | 8 | 5311 | zix 7821 | 8 / 59 |
| HTTP/1.1 | 7 | 1238 | edixoi 6927 | 36 / 59（5 列未跑） |
| Gateway | 6 | 0 | vanilla-epoll 5963 | 未上榜 |

核心观察：

- **H1 / WS 的差距主要是核心数**：cnetmod 每核效率高于榜首（baseline 727 vs zix 691 rps/CPU%，WS 861 vs 702），但只用了约一半 CPU（3,300% vs 6,400%）。
- **H3 是真实的每请求效率差距**：zix 7,096 rps/CPU%、agensio 5,692、cnetmod 925。
- **静态是 H2/H3 家族的最大失分项**，而且 PR 钉住的提交早于静态缓存。

---

## 1. 对手的关键机制

### 1.1 事件循环与 io_uring

| 机制 | zix | ioxide (edixoi) | agensio | vanilla | cnetmod 现状 |
|---|---|---|---|---|---|
| ring 标志 | `SINGLE_ISSUER \| DEFER_TASKRUN \| CQSIZE \| CLAMP` | `SINGLE_ISSUER \| DEFER_TASKRUN \| NO_SQARRAY` | Asio（epoll 封装） | epoll | 仅 `CQSIZE \| COOP_TASKRUN` |
| accept | multishot accept | multishot accept | SO_REUSEPORT | SO_REUSEPORT | 首包 io_uring 后 `accept4` 批量排空 |
| recv | 每连接固定 buf + 一次 `prep_recv`；WS 用 provided buffer ring | multishot recv + provided buffer ring（共享 / incremental 两种） | 每 worker UDP socket + `recvmmsg` 16 slot，**UDP_GRO** | epoll 批读 | 共享 multishot recv 服务（已实现） |
| 完成分发 | 批量 `copy_cqes` ≤512，按 user_data switch | CQE 在 reactor 线程同步分发，续体内联 | `bind_immediate_executor` 内联，不进 post 队列 | 单线程批处理 | 协程 post 队列，存在跨阶段唤醒 |
| 等环策略 | `wait_nr=count/2` + 20µs 窗口；每 16 CQE 提前 submit | 一次 `enter(GETEVENTS|EXT_ARG)` | — | — | 固定批大小，可调但未自适应 |
| per-request syscall | 0（recv/send 共用 enter） | 0 | 0 | 0 | 0（但唤醒次数更多） |

**可复用结论 1**：`IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_DEFER_TASKRUN` 是低风险、全局收益的开关（要求 setup 与 enter 同线程）。cnetmod 的 ring 由固定 worker 线程驱动，满足条件；EINVAL 时按 ioxide 的方式回退。

**可复用结论 2**：自适应 `wait_nr`（约 CQE 数一半）+ 深批提前 submit，可减少高负载下 `io_uring_enter` 次数。

### 1.2 HTTP/1.1 写路径

- **zix "staged send sink"**：每个连接一块 16 KiB→1 MiB 的发送 slab；dispatch 内把 N 个流水线响应全部 append，结束时**一次 `prep_send` 发整段**（不是 writev）。短写只推进 offset 再发。释义见 `src/tcp/http1/core.zig:1008,1080-1097,1166-1172` 与 `dispatch/uring.zig:744-748`。
- **vanilla**：`drain_requests` 把一整批响应写进 write_buf，每个 burst 一次 flush（`async_linux.c.v:905,1031-1096`）；WS 一次调用内把整批帧 append 进同一 out 后单次 send。
- **edixoi**：一个响应 = 一次 SEND；溢出段用一条 SENDMSG gather；partial 才重提。
- **cnetmod**：HTTP/1.1 已在 receive 批次内合并提交（`http_server` 连接级缓冲），这是对的；WS 仍每帧一次写，pipelines 的 per-request 分配（`std::string pending_responses`、thread_local map）仍比 zix 碎。

**可复用结论 3**：WS 帧循环改为"整批入缓冲 → 一次 flush"；H1 检查响应构建是否仍产生 per-request 字符串分配（zix 是写入 slab 的定长十进制/常量表）。

### 1.3 HTTP/3 / QUIC（最大差距）

agensio 的每请求路径（`src/quic/udp.hpp`、`connection.hpp`、`src/http3/connection.hpp`）：

1. **每 wake-up 一次 `sendmmsg`**：所有连接的应答先写进每 worker 的 `SendBatch`，末尾统一发送（`udp.hpp:507-510`）。
2. **GSO 合并**：同 peer、同长度的连续 datagram 折叠成一条 GSO 消息（≤64 段），EIO/EINVAL 永久回退逐包（`udp.hpp:112,76-85,173-183`）。
3. **ACK 骑在数据包上**：响应包顺带确认请求，避免 25 ms 后单独 ACK 包（`connection.hpp:1227-1236`，`recovery.hpp:177-180`）。
4. **原地 seal 进 batch 缓冲**：每把 key 只建一次 `EVP_CIPHER_CTX`，每包只换 nonce，无临时包缓冲（`crypto.cpp:107-118,141-151`）。
5. **一包多流**：一个包最多塞 64 个 stream 的 STREAM 帧；每连接一次最多 128 datagram（`connection.hpp:401,1360-1414`）。
6. **响应体零中间拷贝**：`MemoryBody` 直接从 body 视图 memcpy 进正在加密的包（`connection.hpp:1493-1505`）。
7. **流对象池**：每连接 `StreamPool`，release 时 reset 保留容量，下个请求复用（`src/http/stream_pool.hpp:24-49`）。
8. **请求→响应全内联**：帧处理、handler、respond、produce 在同一轮 drain 内同步完成，无 post、无协程跳转（`http3/connection.hpp:521-617`）。
9. **UDP 收包**：`recvmmsg` + **UDP_GRO**，一次读按 cmsg 拆多个 datagram（`udp.hpp:298,477,489-503`）。
10. **每 worker UDP socket + BPF 按 DCID 首字节分流**，连接不跨 worker（`udp.hpp:307-315,369-380`）。

zix 的 H3 同构：

- multishot recvmsg + provided buffer ring（`src/udp/http3/dispatch/uring.zig:204-211,311-324`）
- 双 `SendBatch`（32 slot），AEAD 原地 seal 进 batch slot，再以 sendmsg 上环；`UDP_SEGMENT` 合并（`src/udp/datagram.zig:419-436`）
- **多个小响应合并进一个 1-RTT 包**（约 1100 B，一次 seal）（`dispatch/common.zig:72-76,1434-1437`）
- 每个收到的 datagram 最多解析 96 个请求（`http3/request.zig:70`）
- QPACK 只用静态表（`http3/response.zig:7-8,96-127`）

**cnetmod 对照**：

- `async_send(span)` 会拷贝成 vector；基准路径使用 move 重载，**不是**主要问题。
- 真正的结构成本：每个响应经 `write_commands` 队列 + `send_flush_requested` 唤醒 writer，每次 flush 最多 16 datagram；ACK 走独立帧；每个流 `make_unique<quic_stream>`；每个请求至少一次协程挂起/完成往返；没有 GRO；没有"一包多流"聚合。
- 已有的优势：收包就地解密、AEAD/AES 缓存、候选密钥定长、ACK 区间紧凑、定时器 deadline 去重。

**可复用结论 4（H3 路线）**：按收益排序
1. 响应发送聚合：一次 wake 内跨流组包，一包多 STREAM 帧；
2. 每 wake 一次 `sendmsg`（+ GSO）；
3. 流对象池化（复用 `quic_stream` 与接收缓冲）；
4. ACK 搭响应包（检查现有 `path_with_pending_ack` 策略）；
5. UDP GRO；
6. 请求→响应减少一次调度跳变（writer 内联尝试）。

### 1.4 静态文件

| | zix | agensio | vanilla | cnetmod |
|---|---|---|---|---|
| 缓存 | 30 s TTL 文件缓存（arena 入口设置）；命中不 open/stat | 进程级共享 cache + **每 worker 无锁 LocalIndex**；1 s 重验证 | 启动快照 + 100 ms 重验证，签名 (dev,ino,size,mtime_ns,ctime_ns) CAS 发布 | 文件内容缓存，`revalidate_interval=500 ms` |
| 头 | 插入时预渲染 | H3 预编码 QPACK 块；H1/H2 缓存头 | 头先写入 out，再交 fd | 每请求重建 header map（H3 已预编码） |
| body | H1 sendfile；H2 mmap 拷进 64 KiB 合并缓冲 | H3 memcpy 进 QUIC 包；H1/H2 sendfile/writev | `sendfile`（≥256 KiB），kTLS 下 `MSG_MORE` 让头与 sendfile 同一 record | **io_uring 后端没有 sendfile**，用户态拷贝 |
| 每请求 syscall | 0 | 0（最多 1 次/秒 stat） | 0 | 0 文件 syscall（缓存命中），但每请求构建头 + 拷贝 body |

注意：zix 的 meta 描述称"static opens the file per request"，但其 arena 入口实际设置了 30 s 缓存；static-h2 的 1.69M 来自常驻 fd + 预渲染头 + mmap/sendfile。

**可复用结论 5**：静态路径三件事
1. io_uring 后端补 `sendfile`（或 `IORING_OP_SPLICE`/`SEND_ZC`）；
2. HTTP/1.1 与 HTTP/2 复用预编码头（H1 预序列化字节、H2 预编码 HPACK）；
3. 命中路径避免任何 `std::string`/header_map 重建；大文件走 sendfile，小文件走缓存字节。

### 1.5 定时器与异步 profile

- agensio：每 endpoint **一个** `steady_timer` + 二叉堆，只在最早 deadline 变化时重 arm（`quic/timer_heap.hpp:2-5`）。
- vanilla：V 运行时按请求创建 timerfd（并非"free-list"；子代理核对后更正）。
- cnetmod epoll 后端：每次 `async_timer_wait` 都 `timerfd_create` + `settime` + close（`executor/platform/epoll_async_op.cpp:880-903`）。io_uring 用 `IORING_OP_TIMEOUT`，可接受。
- 影响：未跑的 `async`、`latency-*` profile 正依赖这条路径，若在 io_uring 上运行问题不大；epoll 回退环境需要 timerfd 复用。

### 1.6 套接字与 TLS

- zix：`SO_BUSY_POLL=50µs` + `TCP_NODELAY`（`dispatch/uring.zig:766-767`）。
- edixoi：**kTLS TX**（`SOL_TCP/TCP_ULP="tls"`，`TLS_TX`），io_uring 明文 SEND 由内核出 record（`Tls/Interop/Ktls.cs:83-91,166`）。
- vanilla：kTLS + `MSG_MORE` 把头和 sendfile 数据并入同一 TLS record。
- cnetmod：已有 kTLS（仅 TLS 1.2 AES-GCM，Linux），但没有 `MSG_MORE` 合流，TLS 写仍是逐块 `ssl_stream`。HTTP/2/1.1 TLS profile 可评估显式开启 kTLS + 头体合流。

### 1.7 Worker 数与 CPU 亲和

- vanilla：`runtime.nr_cpus()`（不看 affinity），可用 `VANILLA_WORKERS` 覆盖。
- HttpArena 容器分配约 64 逻辑 CPU，榜首 H1/H2/WS 普遍跑到 6,200–6,400%。
- cnetmod arena 适配器 `available_processors()` **只取物理核**（`frameworks/cnetmod/server.cpp:805-846`），因此约 3,300%。
- **这是 H1/WS 差距的主因，也是一行改动。**

注意：agensio/zix 的 H3 CPU 只有 1,281–1,516%，说明它们的 H3 不是靠堆核，而是靠每请求成本。

---

## 2. cnetmod 优化清单（按预期收益/风险）

### P0：不改框架，先拿分

1. **适配器使用全部 cpuset 逻辑核**：`entrypoint.sh` 导出 `CNETMOD_HTTP_WORKERS="$(nproc)"`（或把 `available_processors()` 默认改为全部 eligible）。预计 H1 baseline 2.4M→4.4M、WS 2.8M→4.3M+、H2 13M→15–16M。
2. **更新 PR 的 cnetmod pin** 到最新 develop（含静态缓存 `b22c1df` 与本轮 4 个提交），重新跑完整 profile。本地实测 static-h3 +95%。
3. **补齐 5 个未跑的 H1 计分列**（async、latency-10k、latency-1m、json-tls、8gbit、async-db 及 gateway/production-stack）。缺列直接计 0。

### P1：框架内、低风险

4. **io_uring 标志**：`SINGLE_ISSUER | DEFER_TASKRUN`（EINVAL 回退），并增加自适应 `wait_nr` 与深批提前 submit。预期所有 Linux profile 的每请求 CPU 下降。
5. **io_uring sendfile**：为静态大文件补齐 `sendfile`（Linux io_uring 支持 `IORING_OP_SENDFILE`；或退化为 splice）。配合静态缓存，static-h2/h3 与 static-tls 都会受益。
6. **HTTP/1.1 + HTTP/2 预编码静态头**：H1 预序列化状态行+头，H2 预编码 HPACK 块；命中路径零 header map 重建。
7. **WS 批量写**：一次事件内把整批帧 append 到连接缓冲，单次 flush。
8. **H3 发送聚合**（两步走）：
   - 第一步：一个 flush 周期内跨流组包，让一个 1-RTT 包承载多个 stream 的 STREAM 帧（现在按流写命令）。
   - 第二步：每 wake 一次 `sendmsg` + GSO 合并同 peer 连续 datagram。

### P2：结构性、需要基准验证

9. **流对象池**：复用 `quic_stream`（保留接收缓冲容量），消除每请求 `make_unique`。
10. **ACK 搭车**：响应包顺带 pending ACK，减少单独小包（先确认当前策略）。
11. **UDP GRO**：`UDP_GRO` + cmsg 拆包，降低每 datagram 接收成本。
12. **请求→响应少一次调度跳变**：评估在 writer 循环内直接执行可同步完成的 handler 的可行性（风险较高，需保持 SSE/streaming 语义）。
13. **SO_BUSY_POLL**：只在基准/低延迟 profile 评估，避免空闲耗 CPU。
14. **kTLS 合流**：TLS 响应头与 body 用 `MSG_MORE`/sendfile 合并。仅 TLS 1.2 路径，需单独验证。

---

## 3. 不建议照搬的东西

- **zix 的 per-request open 静态行为**：其榜上成绩实际来自缓存，不要据此认为"不缓存也能快"。
- **agenasio 的单一 sendmmsg 批量**：它建立在"每 worker 一个 UDP socket + BPF 分流"之上；cnetmod 若改成单 batch，必须先保证连接不跨 worker、且发送缓冲生命周期覆盖整批。
- **vanilla 不看 affinity 的 nr_cpus()**：在容器里可能造成 2× 超订；cnetmod 应使用 `nproc`（尊重 cpuset）。
- **CUBIC 定点化 / 无 ACK 延迟**：会改变 RFC 行为与拥塞轨迹，需单独 A/B，不与结构改动混提。

---

## 4. 参考源码索引

| 主题 | 位置 |
|---|---|
| zix io_uring 循环 | `zix/src/tcp/http1/dispatch/uring.zig:31-37,122-152,651,744-748,1465-1523` |
| zix staged send | `zix/src/tcp/http1/core.zig:1008,1080-1097,1166-1172` |
| zix H3 batch/GSO | `zix/src/udp/http3/dispatch/uring.zig:64-90,1331-1349`、`zix/src/udp/datagram.zig:419-436` |
| zix 静态 | `zix/src/utils/static_cache.zig:394-421`、`static_send.zig:55-75`、`tcp/http2/static.zig:154-163` |
| agensio QUIC 收发 | `agensio/src/quic/udp.hpp:76-131,298,477-510,712` |
| agensio 连接/组包 | `agensio/src/quic/connection.hpp:388,401,1227-1236,1360-1414,1493-1505` |
| agensio 流池 | `agensio/src/http/stream_pool.hpp:24-49`、`src/http3/connection.hpp:86-92` |
| agensio 静态 | `agensio/src/cache.hpp:4-13,54-60,161-183`、`src/http3/static.cpp:137-151,229-268,531-636` |
| ioxide ring | `ioxide/src/ioxide/io_uring/Ring.cs:118-235` |
| ioxide multishot recv | `ioxide/src/ioxide/Reactor.Tcp.cs:17-19,71-126` |
| ioxide kTLS | `ioxide/src/ioxide/Tls/Interop/Ktls.cs:83-91,166` |
| vanilla 静态/kTLS 合流 | `vanilla/static_assets.v:35-51,652-770`、`tls_conn_linux.c.v:114-127` |
| vanilla WS/H1 批量写 | `vanilla/examples/websocket_echo/src/main.v:103-158`、`async_linux.c.v:905,1031-1096` |

---

## 5. 实施记录（2026-10-21 晚）

### 已落地并测试（cnetmod develop，已推送）

| 提交 | 内容 | 验证 |
|---|---|---|
| `31180d89` → `11a9369c` | io_uring 单 issuer 实验：`SINGLE_ISSUER\|DEFER_TASKRUN` + 惰性建 ring | **已回退**：HTTP server 从 accept 线程给每个 worker ring 注册接收缓冲，单 issuer ring 直接 `-EEXIST` 并停掉 reactor；惰性创建也与该跨线程 setup 竞争。保留 `stop()` 走 pipe 唤醒 |
| `f5a59b58` | QUIC 每次 flush 的 datagram 预算 16 → 64 | QUIC/HTTP3 全套通过 |
| `ba27ad19` | HTTP/2 `write_response` 仅在跨线程时 post | H2/HTTP3 测试通过 |
| `5b249db2` | H2 静态响应复用预编码 HPACK 块（capacity 0 → literal-without-indexing），新增索引不漂移单测 | 7 项 HPACK 单测 + 全套通过 |

适配器侧：`E:\github\HttpArena-cnetmod` 的 Dockerfile pin 从 `2966aba` 更新到 `11a9369c`，已推送 fork（PR #1595 会自动更新），等待维护者重新触发 `/benchmark --save`。

### 关键实测修正

1. **“只用半个 CPU” 的假设被本地 A/B 推翻。**
   本机 32 vCPU（WSL）上，同一适配器二进制：
   - 256 连接：物理核 354k/370k vs 全部逻辑核 332k/330k；
   - 1024 连接：物理核 460k/445k vs 全部逻辑核 450k/439k。
   SMT 兄弟线程没有带来吞吐收益。因此没有把 `CNETMOD_HTTP_WORKERS` 设为 `nproc`，保持每物理核一个 worker。榜单上榜首 6,400% CPU 应理解为“更多逻辑线程/忙轮询”，而不是等比例的吞吐来源。
2. **静态缓存效果（本地 static-h3，4 KB 文件，16 workers）**
   - `2966aba`（PR 原 pin，无缓存）：34.7k
   - `2ed655e4`（静态内容缓存）：54.3k（+56%）
   - 当前 develop（+预编码 HEADERS）：67.7k（+95%）
3. **本地适配器验证**：H1 `baseline2` 返回 200 且吞吐 ~350–460k（WSL，非榜单硬件）；H2 over TLS `/static/app.js` 返回 2xx，说明预编码块在真实适配器路径上工作正常。

### 仍未解决（决定能否 H3 第一）

- H3 每请求 CPU 仍是 zix/agensio 的 5–7 倍。本地 perf 显示热点高度分散（字符串操作 ~6%、vector/分配 ~4%、Huffman ~1.5%、协程恢复 ~2%），没有单点可打，需要结构性改造：流对象池、请求头 arena、响应批量组包、UDP GRO、减少每请求调度跳变。
- io_uring `SINGLE_ISSUER|DEFER_TASKRUN` 的前提是先让 HTTP server 把每 loop 的接收服务注册投递到其所属循环；完成后再用 `CNETMOD_IOURING_SINGLE_ISSUER=1` 验证。
