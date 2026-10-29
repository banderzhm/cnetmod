# QUIC / HTTP/3 热路径性能审查

- 日期：2026-10-21
- 基线：`origin/develop` @ `2ed655e4 perf(quic): compact recovery hot-path state`
- 审查范围：`src/protocol/quic/`（27.5k 行中约 20k）、`src/protocol/http/v3/`、UDP 收发胶水层
- 基准负载：h2load `-c 64 -m 64 -t 16 -D 10s`，ALPN `h3`，16 worker，约 1.6M req/s；请求为小 GET
- 方法：三组并行只读审查（连接/收发路径、HTTP/3 会话与 QPACK、加密与发送管线），报告中的每条发现均已由审查者逐行核对源码；本文档汇总时做了交叉印证

> 本文只列**可执行**的优化点，并标出已被实测否决、不要再试的方案。所有改动都必须先满足：RFC 行为不变、极端输入安全、A/B 实测。

---

## 0. 结论摘要

| 优先级 | 条目 | 预期收益 | 风险 |
|---|---|---|---|
| P0-1 | 收包路径每包 2 次堆分配 + 2 次全包拷贝 → 就地解密 | 高 | 低-中 |
| P0-2 | 头保护每包重算 AES key schedule → 预计算 | 中高 | 低 |
| P0-3 | 每包 `atomic_load(shared_ptr)` 取 AEAD ctx → 裸指针 | 中高（多核） | 低 |
| P0-4 | 每请求 `spawn()` 3 次分配 + 绕一轮 reactor → 就地 resume | 高 | 中 |
| P0-5 | 每请求 `completed_headers_[id]` 插入空 deque → find 后再插入 | 高 | 低 |
| P1 | ACK 编解码三次分配、`to_string()` 比较 peer、每包多次 `now()`、无 `co_await` 的协程帧、有序 STREAM 进 map、QPACK 编码、定时器 deadline 每包重发布、CUBIC 浮点 | 中 | 低-中 |
| — | UDP GSO 已实现但**默认关闭**（单点最大收益，需评测环境支持） | 高 | 低 |
| — | Windows 无 RIO 时逐条 `WSASendTo`，无 GRO | 平台缺口 | — |

---

## 1. QUIC 连接/收发包热路径

审查文件：`src/protocol/quic/quic_connection.cpp`（6373 行）、`quic_connection.cppm`。
路径：1-RTT 收包 → 解密 → 帧分发 → `push_received` → ACK → packetize → flush。

### 1.1 【高】收包每包 2 次堆分配 + 2 次约 1.3 KB 拷贝

- 位置：`quic_connection.cpp:2422`，`quic_crypto.cpp:1706/1723`
- 现象：短头路径对每个候选密钥执行 `std::vector<std::byte> wire(packet.begin(), packet.end())`；随后 `open_payload` 再分配一个明文 vector。每收一个 1-RTT 包至少 2 次 malloc/free + 2 次全包 memcpy；密钥轮换期候选为 2 时，wire 拷贝翻倍（且失败候选仍需重新拷贝）。
- 修复：仿照发送侧就地加封，新增 `open_payload_into(span<std::byte> out)` / `open_packet_payload_in_place`，直接对池化接收 buffer 解保护 + 解密（BoringSSL 允许 in/out 重叠）。多候选时只需保存/恢复被修改的头部字节，不必整包复制。
- 前提确认：接收 buffer（`udp_datagram_buffer`，move-only 独占租约）在 `process_datagram` 期间独占，明文当前就是局部的，生命周期等价。
- 风险：低-中（长头合包递归路径不要共用缓冲）
- 实测佐证：现有 perf 记录显示 `vector<byte> insert` 2.19% + `operator new[]` 2.07%（`testing/bench/results/httparena-h3/2026-10-18-hotpath/diagnostic/current-perf-offline-recovery/`）

### 1.2 【中高】每次 flush 校验与发布定时器 deadline

- 位置：`quic_connection.cpp:739-757`、`:1695-1728`
- 现象：`listener_state_guard` 析构时无条件调用 `next_timer_deadline()`（PTO/路径扫描 + `can_start_requested_flush()` 的 try_lock）。有数据/待 ACK 时返回 `now`，观察者（`http3_server.cpp:1326`）因此每包做一次 latch 独占锁 + map 查找 + priority_queue push（deadline=now），并可能 `wakeup.cancel()` —— 即每包一次定时器队列插入和跨协程唤醒。
- 修复：在 impl 缓存"上次发布值"，deadline 未变小则不发布；或在 listener 侧用"有帧待发"标志，批量收包后直接 flush，不插入 now-entry。
- 风险：中 —— 必须保留直接 flush 路径，漏掉 now 边沿会延迟 ACK。

### 1.3 【中高】ACK 每包 3 次分配

- 位置：`quic_connection.cpp:4411/4414`、`:229-250`、`quic_frame.cpp:655-676`、`quic_frame.cppm:84`
- 现象：发送侧 `take_ack_frame` 向 `ack_frame::ack_ranges`（vector）push_back；`encode_frame(*ack)` 再分配临时 vector 后拷进 payload；接收侧 `decode_frame`（`quic_frame.cpp:67`）再 reserve+push 一次。h2load 下几乎每个数据包都对应一个 ACK。
- 修复：`ack_range` 改内联 `std::array<ack_range,4>` + 计数（超出走堆）；新增 `encode_ack_into(span<std::byte>, received_packet_ranges)` 直接写入 payload。
- 风险：对端可给任意 `ack_range_count`，必须有上界与回退，否则栈/内存攻击。

### 1.4 【中】每次 flush 分配两个 vector

- 位置：`quic_connection.cpp:3888-3889`、`:3958-3959`
- 现象：`pending` 与 `datagrams` 每次 `pack_and_send_packets()` 都 heap 分配（一次收包基本触发一次 flush）。scheduler/frames 已是 16 个批量。
- 修复：改为 impl 成员 `std::array<pending_datagram,16>` + count 与复用 `datagrams`（`flush_mutex` 已串行化发送）。
- 风险：低

### 1.5 【中】每包两次 `to_string()` 比较 peer

- 位置：`quic_connection.cpp:2546`（以及 :2472/:2485 的非 0 path）
- 现象：`sender.to_string() != impl_->peer.to_string()` 每包构造两个 `std::string`（IPv4 两次 `inet_ntop`，IPv6 还会堆分配）。`endpoint` 目前没有 `operator==`（`address.cppm:214`）。
- 修复：给 `endpoint`/`ip_address` 加 `operator==`（v4/v6 联合 + port），或缓存 peer 格式化串。
- 风险：无

### 1.6 【中】每包多余协程帧

- 位置：`quic_connection.cpp:2628`（`handle_short_header_packet`）、`process_frames` 的 visit 协程
- 现象：`handle_short_header_packet` 是 `task<>` 但函数体内没有任何 `co_await`，每个数据包白分配/释放一个协程帧；`process_packet` 慢分支还会为每个非 ACK/STREAM 帧分配一个协程帧。
- 修复：改成返回 `std::expected` 的普通函数；`process_frames` 改同步分发器，仅确有 `co_await` 的分支返回 `optional<task<>>`。
- 风险：低

### 1.7 【中低】每包一个读密钥候选 vector

- 位置：`quic_crypto.cpp:1263-1278`，调用点 `quic_connection.cpp:2401`
- 现象：`application_read_key_candidates()` 按值返回 vector，稳态只有 1 个元素仍 malloc。
- 修复：`std::array<application_read_key_candidate,3>` + count，或调用方传入固定缓冲 / 回调遍历。
- 风险：无

### 1.8 【中】有序 STREAM 数据也进 `std::map`

- 位置：`quic_connection.cpp:3512` → `quic_stream.cpp:361/368`
- 现象：即使 `offset == next_expected_offset_` 的完全有序片段，也 `try_emplace` 到 `std::map<uint64_t, vector<byte>>`（map 节点 + payload vector 两次分配 + 拷贝），读者读走后再 erase。每请求体/每数据帧一次。
- 修复：增加连续缓冲（vector + 已消费水位，或连续 chunk deque），仅遇到空洞时才用 map。
- 风险：中 —— 重叠字节比对与 final_size 语义必须逐字节保持一致。

### 1.9 【低-中】每包 3~4 次 `steady_clock::now()`

- 位置：`quic_connection.cpp:2461`、`:2537`、`:6354`、`:1701`，收到 ACK 时还有 `:3119`
- 修复：`process_datagram` 取一次 now 逐层下传（recovery/ACK/pacing 共用同一采样）。
- 风险：无（同一包内同一时刻更正确）

### 1.10 【低】每包调用 `SSL_process_quic_post_handshake()` + `ERR_clear_error()`

- 位置：`quic_connection.cpp:2534`
- 现象：稳态下 BoringSSL 快速返回，但每包一次 TLS 入口与线程局部错误队列触碰。
- 修复：仅在握手后存在待处理 post-handshake 数据时调用（或每收包批次调用一次）。
- 风险：低 —— 需确认 NewSessionTicket / key update 仍被驱动。

### 1.11 【低-中】每次 flush 的流扫描与主动 `post_awaitable`

- 位置：`quic_connection.cpp:4927-4961`、`:3532`
- 现象：命中 ≥2 个活跃双向流（`-c 64` 必然命中）时，每次 flush 额外绕一次 reactor；另有 16 条 unordered_map 遍历。另外每个 STREAM 帧都做一次 `readiness->try_send`（channel 内 mutex）。
- 修复：把该 hint 合并进 listener 已有批处理/定时唤醒；readiness 通知按"上次通知后数据仍可读"去重。
- 风险：低，保留单流快路径。

### 已确认非热路径（不要误改）

无日志字符串格式化；`retired_stream_history` 只在 retire 时插入；`pending_path_validations` 的 `erase_if` / `to_string` 只在 multipath 路径验证生效时才有每包开销；`application_paths` 是 1 元素 `std::map`，其 `.at()`/遍历可忽略。

---

## 2. HTTP/3 会话 / QPACK / 静态文件

审查文件：`src/protocol/http/v3/http3_session.cpp`（4490 行，含 `:cppm`）、`http3_server.cpp`、`qpack.cpp`，以及 `frame/buffer/flat_map/mutex/spawn` 相关实现。

### 2.1 【高】每请求 `spawn()` 3 次分配 + 绕一轮 reactor

- 位置：`http3_session.cpp:1377`，`coro/spawn.cppm:69-72`
- 现象：`spawn(conn_.context(), service_peer_stream(...))`：inner task 帧 + 包装协程帧 + `make_unique<detached_post_state>` 共 3 次堆分配，且已在本 loop 线程上仍 post 一轮 reactor。
- 修复：`ctx.running_in_this_thread()`（`io_context.cppm:76` 已存在）时内联 resume（`dispatch_guarded` 已有此模式），或直接 `ctx.post(coroutine_handle)` 省掉 state 分配。
- 风险：中 —— 内联后请求可能同步跑完，需确认不会重入 accept 循环 / 不会在 `peer_streams_.wait()` 之前同步耗尽。

### 2.2 【高】每请求插入空 `std::deque`

- 位置：`http3_session.cpp:1765`（另 1591/1648/1871）
- 现象：`completed_headers_[id]` 用 `operator[]`，每次请求都往 flat_map 插入一个空 `std::deque`（deque map/block 堆分配），只有 QPACK blocked 才需要；1416 又 erase。
- 修复：先 `find`，只有 `resource_unavailable_try_again` 时才建条目，普通路径传局部空 deque。
- 风险：低

### 2.3 【高】响应 wire 组装多次分配与拷贝

- 位置：`http3_session.cpp:719-729`、`:2268`
- 现象：QPACK `block` → `encode_http3_frame` 再分配+复制 → `reserve(size+body+16)` 必然 realloc 再复制 → body 再复制；shared_body 路径 2268 又把 header 复制进 prefix vector。
- 修复：提供 `encode_into(byte_buffer&)` 原地回填 frame 长度，`response_frames` 预 reserve，`async_send_shared` 直接收 `byte_buffer`。
- 风险：低（注意 varint 长度回填）

### 2.4 【高｜static-h3 专项】静态文件每请求重建头块并重新 QPACK 编码

- 位置：`static_files.cpp:95-99`、`http3_session.cpp:753-783`
- 现象：`populate_response` 每请求重建 `header_map`（3-4 次插入 + `to_string`），然后每请求重新 QPACK 编码同一头块，完全未使用已有的 `qpack_static_response_headers`（session 已支持）。
- 修复：在 `cached_static_file` 按 content-encoding 变体缓存 `static_headers`，并预算好 HEADERS+DATA 前缀，配 `shared_body` 一次发送。收益主要在 static-h3 场景。
- 风险：低（该路径要求 headers 为空、status 匹配，且 declared_length 依赖 headers，需同步调整）

### 2.5 【中高】QPACK `append_string` 短值也先做 Huffman vector

- 位置：`qpack.cpp:188`
- 现象：每个非静态值调用 `huffman_encode(string_view) -> vector<byte>`，即使 Huffman 更长也会先做一次堆分配。
- 修复：加写入栈缓冲（≤64 B）的重载，长值才回退 vector；短值可跳过 Huffman 尝试。
- 风险：低

### 2.6 【中】QPACK 编码无动态表时的多余工作

- 位置：`qpack.cpp:563-592`
- 现象：默认 capacity==0（无动态表）仍构造 `representations` 向量；每个头做 2 次 `detail::exact` + 1 次 `detail::name`（各含 flat_map 查找 + 线性 value 扫描），且 `out` 无 reserve（小响应约 5 次 realloc）。
- 修复：capacity==0 单趟编码并复用一次静态查找结果；`out.reserve(32 + 8*n)`。
- 风险：低

### 2.7 【中】QPACK 解码预留过大并复制静态表项

- 位置：`qpack.cpp:888-903`
- 现象：按 `min(块字节数,16)` 预留 `header_field`（每项 64 B，约 1 KB/请求），静态表的 name/value 再复制成新 string（含空值）。
- 修复：按字段数上限 6-8 预留或复用连接级 scratch；伪头直接移入请求结构。
- 风险：低

### 2.8 【中】每请求 retire 一个 map 节点

- 位置：`http3_session.cpp:1420` → `quic_connection.cpp:83, 5847`
- 现象：每次 `retire_stream` 向 `std::map recent_` 插入节点（1 次分配/释放），另有 deque push；每请求一次。
- 说明：这是交接文档中保留的 final-size 窗口（1024 全局）。**不要**改回开放寻址或 node-handle 复用（均已实测更慢，见 §5）。
- 可选修复：`recent_` 改分片 vector/ring 或者复用 `ranges_` 的结构（须先 A/B）。
- 风险：低

### 2.9 【中】每请求取消令牌登记进 map

- 位置：`http3_session.cpp:2008-2013`、`quic_connection.cpp:523`
- 现象：async/streaming handler 路径每请求 `register_stream_cancellation` 往 `std::map` 插入节点（RESET 取消语义依赖它）；sync 基准路径不受影响。
- 修复：改 flat_map 或按 `sid/4` 的 `vector<cancel_token*>`。
- 风险：低（必须保留 reset 时 cancel 的行为）

### 2.10 【中｜报文级】每个入站数据报两次哈希查找 + 取 now

- 位置：`http3_server.cpp:1772-1773`（另 1432-1448、1564-1575）
- 现象：每个入站数据报都做 shared_latch + `sessions_` 与 `published_route_generations_` 两次哈希查找，`select` 还每报文取 `steady_clock::now()`；refresh 在 generation 未变时虽早退，但仍先取锁查表。
- 修复：在 connection/inbox 上缓存 `session_ready` 与 last_published_generation，仅新流/CID 变化时复查。
- 风险：低

### 2.11 【低中】每请求无条件 flush QPACK decoder 指令

- 位置：`http3_session.cpp:1594/1769`
- 现象：静态表块无 decoder 指令；若编译器未做帧省略，就是一次协程帧分配。
- 修复：先查 `decoder` 是否为空再调用（或把空判断上移）。
- 风险：低

### 未发现

每请求日志/格式化、头容器 `unordered_map`、每次请求无谓 shared_ptr 拷贝（除 2260 处有意的共享）、锁竞争型热点（`async_mutex` 无竞争时仅 CAS）。

---

## 3. 加密 / 帧 / 发送管线 / UDP 批处理

审查文件：`quic_crypto.cpp/.cppm`、`quic_packet.cpp`、`quic_frame.cpp`、`quic_send_pipeline.cpp`、`congestion_control.cpp`、UDP 收发胶水（`io_uring_async_op.cpp`、`iocp_async_op.cpp`）。

### 3.1 已确认已优化（无需再改）

- Linux `sendmmsg`：16 条/批（`io_uring_async_op.cpp:1695-1716`）
- `recvmmsg`：首包走 io_uring，之后排空（`:1854-1888`）
- UDP GSO：**已实现但默认关闭**，需显式 opt-in（`quic_foundation.cppm:243-247`、`quic_connection.cpp:1506`、`io_uring_async_op.cpp:1722-1815`）
- 无 UDP GRO
- 发送侧 16 包一个 pacing 定时器（`quic_connection.cpp:3951-3956`），非每包
- 发送明文就地 AEAD 加封（`quic_crypto.cpp:1664-1704`），无二次拷贝
- 帧解析使用 `span`，非拥有容器（`quic_frame.cpp:212+`）
- ACK 范围用紧凑 vector（`quic_connection.cpp:173-223`）

### 3.2 【高】RX 每包两次拷贝 + 每包堆分配

- 位置：`quic_connection.cpp:2422`、`quic_crypto.cpp:1723`
- 与 §1.1 同一问题，独立复核确认：`std::vector<std::byte> wire(packet.begin(), packet.end())` 在全包范围内复制并位于候选密钥循环内；`open_payload` 再分配明文输出。
- 修复：增加就地解密接口（见 §1.1），直接对池化接收 buffer 操作，帧解析仍用 span。
- 风险：中低（需确认 buffer 在 `process_datagram` 期间独占、解析后不被保留）

### 3.3 【中高】每包 `atomic_load(shared_ptr)` 取 AEAD 上下文

- 位置：`quic_crypto.cpp:1565-1572`，被 seal（1606/1642/1684）与 open（1720）每包调用
- 现象：libstdc++ 的 `atomic_load(shared_ptr*)` 内部走全局互斥锁池 + 引用计数原子操作；16 worker 下有无谓锁竞争/缓存行抖动。
- 修复：在 key 安装/派生时主动 `EVP_AEAD_CTX_init` 并缓存裸 `EVP_AEAD_CTX*`（key 代际不可变，key update 时随 `quic_level_keys` 整体替换）；或至少用 relaxed 原子裸指针。
- 风险：低

### 3.4 【中】每包头保护重算 AES 密钥扩展

- 位置：`quic_crypto.cpp:1375-1382`
- 现象：`compute_hp_mask` 每次调用 `AES_set_encrypt_key`（发送 `protect_header` 每包 1 次；接收 `unprotect_header` 每包 ≥1 次）。密钥固定不变，key schedule 完全是重复工作（约 15–30 ns/包，12 TX/响应时放大）。
- 修复：在 `quic_level_keys` 中预计算 `AES_KEY`（ChaCha 分支无需改），或用一次性 init 的 `EVP_CIPHER_CTX`。BoringSSL `AES_KEY/AES_encrypt` 仍可用；注意结构体随 key 一并复制。
- 风险：低

### 3.5 【中低】每包一个读密钥候选 vector

- 位置：`quic_crypto.cpp:1263-1278`
- 与 §1.7 同一问题，独立复核确认。

### 3.6 【中低】每个出站 ACK 一次堆分配

- 位置：`quic_connection.cpp:4411-4417`、`quic_frame.cpp:655-676`
- 与 §1.3 同一问题，独立复核确认。

### 3.7 【中低】CUBIC 每 ACK 浮点 `cbrt` + `pow`

- 位置：`congestion_control.cpp:145-159`
- 现象：CUBIC 是默认算法（`quic_foundation.cppm:268-269`）；每个 ACK 还重算一次 double 除法 pacing（`:113-117`）。BBR 每 ACK 也做 8 元素 `max_element` + `ceil`（`:321-365`）。
- 修复：epoch 起点算一次 `k`，增量式定点/整数逼近；pacing 用整数 bytes/s（`cwnd*1e9/rtt_ns`），只在改变时更新。
- 风险：中 —— 会改变 cwnd 轨迹，必须对齐 RFC 9438 并做 A/B。

### 3.8 【低】每数据报一个 `vector<span>` 合并包列表

- 位置：`quic_packet.cpp:60-63`
- 现象：`coalesced_packet` 使用 vector，实际绝大多数只有 1 个包。
- 修复：`std::array<std::span<const std::byte>,4>` + size。
- 风险：低

### 3.9 【中｜平台】Windows 无批量发送、无 GRO

- 位置：`iocp_async_op.cpp:2439-2455`、`quic_connection.cpp:1484-1497`
- 现象：无 RIO 时逐条 `WSASendTo`；跨 context 时也逐条 `async_sendto_on`。无 `WSASendMsg` 聚合、无 GSO 等价物；接收靠 RIO ring 或 `select` + 逐条 `WSARecvFrom`（`:2377-2400`）。
- 结论：若基准只针对 Linux 可忽略；否则需说明 Windows 吞吐模型不同，或引入 RIO 批处理。
- 风险：中

### 未发现

热路径内无每包 `EVP_AEAD_CTX_init`（仅 `encrypt_packet`/`decrypt_packet` 等非热接口有，`quic_crypto.cpp:1753/1822`）；无每包非堆分配（`std::array` 在栈上）；无每包单独 pacing 定时器；帧解析未拥有化；发送侧无双重拷贝。

主要实测佐证：`vector<byte> insert` 2.19% + `operator new[]` 2.07%（同 §1.1 的 perf 记录）。

---

## 4. 三份报告的交叉印证

| 问题 | 报告 1（连接） | 报告 3（加密/发送） | 结论 |
|---|---|---|---|
| 收包 wire 全包拷贝 + 明文再分配 | §1.1 | §3.2 | **两路独立确认，P0 第一项** |
| 读密钥候选 vector 每包分配 | §1.7 | §3.5 | 确认 |
| 出站 ACK 多次分配 | §1.3 | §3.6 | 确认 |
| `atomic_load(shared_ptr)` AEAD ctx | — | §3.3 | 单路，但与已知 allocator/perf 热点一致 |
| 每请求 spawn / 空 deque / wire 组装 | 报告 2 §2.1-2.3 | — | 报告 2 独有，按请求路径 |

两份报告都指出：发送侧（packetize、AEAD 就地加封、批处理）已经比较干净；当前主要收益在**接收路径**和**每请求固定开销**。

---

## 5. 已实测否决的方案（不要再试）

| 方案 | 结果 |
|---|---|
| final-size 固定开放寻址表 | `recent_final_size_store::insert` 约 2.75%，比红黑树更慢（哈希混合 + 删除 back-shift + 稳态淘汰过重） |
| `std::map` node handle 循环复用 | 三轮 1.479M / 1.565M / 1.508M req/s，明显下降 |
| 直达槽纯包号掩码无限扩容 | 两个巨大稀疏包号掩码冲突时不断翻倍直至 OOM |

`sent-packet` / `loss-detector` 的在途存储（直达槽 + 冷备用表）、接收 ACK 合并区间、`retired_stream_history` 区间 vector 均已落地，属于已完成项。

---

## 6. 建议实施顺序

1. **P0-4 / P0-5**（HTTP/3 每请求固定开销）：改动最小、语义不变、基准收益直接。
2. **P0-1**（收包就地解密）：先确认接收 buffer 的独占性与长头合包路径，再改。
3. **P0-2 / P0-3**（crypto 缓存）：纯内部实现，不改变线上字节。
4. **P1 中低风险项**：ACK 内联编码、`endpoint::operator==`、单次 `now()`、去掉空 `co_await` 协程帧、QPACK 栈缓冲 + reserve。
5. **需要 A/B 的项**：定时器 deadline 发布策略、有序 STREAM 连续缓冲、CUBIC 定点化、`recent_` 结构。
6. 若评测环境支持，单独评估 **UDP GSO** 打开后的收益（已实现，默认关闭）。

每条改动都必须带：正确性单测、极端输入测试（超大 packet number、乱序/重叠 STREAM、恶意 ACK range 数）、Release 基准、perf 归因；负优化立即撤销。

---

## 附录 A：`quic_pmtu_cc_large_response_after_loss` 失败的环境归因

本轮复现两个"历史失败"结论：

- `quic_handshake_confirmation`：在当前 develop 构建上连续 20 次通过，**不复现**。
- `quic_pmtu_cc_large_response_after_loss`：稳定失败（`ACK-validated discovery did not reach configured ceiling`）。

归因过程：

1. 服务器日志与 `proxy-ledger.json` 显示，服务器按 128 B 步长探测到 2864 后停在 8 秒预算边界；`strace` 显示 2608 发了 56 次、2864 发了 51 次，`sendmmsg` 均成功，但代理只收到 1-2 个。
2. 本机 WSL 使用 mirrored 网络：`127.0.0.1` 路由到 `loopback0`（`ip rule` 显示 `iif eth0 ... lookup local` 规则），该接口 **MTU = 1500**，而不是 `lo` 的 65536。`/proc/net/snmp` 显示 `Ip: ReasmFails` 增长，即 IPv4 分片重组失败。
3. 在独立网络命名空间（`unshare -rn`，真正走 `lo`/MTU 65536）中运行同一二进制与同一测试脚本：**`status: passed`**。

结论：该失败是 WSL mirrored 网络的回环 MTU/分片行为导致的测试环境问题，**不是协议实现回归**。CI（Linux）与具备正常回环 MTU 的环境不受影响；本机复现时应使用 `unshare -rn` 或关闭 mirrored 网络。

> 附带发现：该测试的 `--require-diagnostics` 依赖 `PMTU_CC_*` 埋点，而当前源码中不存在这些标记，诊断模式无法使用（ctest 默认不启用诊断，故不影响 CI）。

---

## 7. 实施结果（2026-10-21）

分支 `develop`，4 个提交（基于 `2ed655e4`）：

| 提交 | 内容 |
|---|---|
| `1dc85ec4` | 每请求不再插入空 deque；新增 `spawn_local()`，已在所属循环时就地启动请求流 |
| `3f3ee472` | 收包单次拷贝 + 就地解密；AEAD ctx 裸指针缓存；AES 头保护密钥表按 key 代缓存；候选密钥改定长数组；`endpoint::operator==` 替代每包两次 `to_string()` |
| `d720152b` | ACK/PATH_ACK 直接追加进 payload；发送批量改栈数组；定时器 deadline 去重发布；短头后处理去掉空协程帧；QPACK Huffman scratch/reserve/无动态表快路径；响应 HEADERS+DATA 单缓冲组装；静态文件按资源预编码 HEADERS 块 |
| `e0361544` | QPACK decoder 无指令时跳过一次协程调用；每包只取一次 `steady_clock::now()` |

### 未实施项与原因

| 条目 | 原因 |
|---|---|
| 有序 STREAM 数据绕开 `std::map`（§1.8） | 需要同时改造读取路径与重叠/final_size 语义；基准为无请求体的 GET，收益不在当前热路径 |
| `coalesced_packet` 固定数组（§3.8） | 仅长头/握手路径使用，稳态不触发；且会改动公开结构 |
| http3_server 每数据报两次哈希查找（§2.10） | 单次成本仅为共享锁与哈希查找，改动需引入跨连接的 session 就绪缓存，风险/收益不匹配 |
| 每请求取消令牌 map（§2.9） | 仅 async/streaming handler 路径，基准 sync 路径不经过 |
| CUBIC 浮点化定点（§3.7） | 会改变 cwnd 轨迹，必须单独 A/B 与 RFC 9438 对齐 |
| Windows 批量发送/RIO（§3.9） | 独立平台工程，不影响 Linux 基准 |
| `recent_` final-size 结构（§2.8） | 已由实测否决的替代方案约束，暂保留 |
| 每包 `SSL_process_quic_post_handshake`（§1.10） | BoringSSL 无“是否有待处理数据”的查询接口，无法安全跳过 |

### 验证

- `test_quic_*`、`test_http3_*`、`test_http3_qpack`、`test_http3_frame` 共 30 个单元测试通过；
- QUIC/HTTP3 集成测试 41 项全部通过（在独立网络命名空间内，`unshare -rn`，规避 WSL mirrored 网络回环 MTU 问题）；
- `test_http3_frame` 的静态文件用例改为解码预编码 HEADERS 块，断言线上可见字段（`:status`、`content-type`、`content-encoding`、`content-length`）。

### A/B 基准

同机、同一构建树、Release、BoringSSL QUIC、16 server workers（CPU 0-15）、h2load 1.69.0（CPU 16-31）、`-c 64 -m 64 -t 16 -D 5s --warm-up-time=2s`、每轮 3 次、错误率均为 0。基线二进制由同一构建树在 `2ed655e4` 源码上编译，避免工具链差异。

**baseline2 应用负载（每请求计算查询参数和）**

| 轮次 | 基线 median RPS | 优化 median RPS | 提升 |
|---|---:|---:|---:|
| 第 1 轮 | 260,649 | 293,244 | +12.5% |
| 第 2 轮 | 275,186 | 311,665 | +13.3% |

服务端 CPU 占用两版相当（约 1370–1470%），即同等 CPU 下吞吐提高约 13%。

**static-h3（4 KB 静态文件，命中缓存）**

| 版本 | Run 1 | Run 2 | Run 3 | Median |
|---|---:|---:|---:|---:|
| 基线 | 56,696 | 51,804 | 54,268 | 54,268 |
| 优化 | 67,702 | 67,973 | 65,633 | **67,702（+24.8%）** |

静态负载收益明显高于应用负载，与“预编码 HEADERS + 跳过一次 QPACK 编码与头表构建”的预期一致。绝对值低于历史 WSL 记录（约 1.35M/s），差异来自当前机器的 WSL/构建环境与负载生成器状态；A/B 两版使用完全相同的环境与构建参数，相对结果仍成立。

### 关于预编码静态头的两个边界

- **动态头共存**：`response_headers_frame` 要求 `static_headers` 存在时 `response.headers` 为空，因此该路径只用于“响应头完全由资源决定”的场景（当前静态文件处理器没有 `Set-Cookie`、`Date` 等请求级头）。若将来需要追加动态头，必须扩展发送器以拼接静态块与动态块，而不能直接往 `response.headers` 里塞。
- **条件请求/304**：HTTP/3 静态文件处理器目前没有实现 `If-None-Match` / `If-Modified-Since`，不存在复用 200 头块回答 304 的风险；若后续加入条件请求，304 必须走独立路径，不能复用 `status::ok` 的预编码块。
