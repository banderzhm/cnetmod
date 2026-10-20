# QUIC 最终只读审查记录

2026-10-18。本记录审查当前未提交的七个文件差异：
`congestion_control.cpp/cppm`、`loss_detection.cpp/cppm`、
`quic_recovery.cpp/cppm`、`quic_connection.cpp`，以及最终 PMTU fixture、
Python driver、显式 AEAD 测试 peer 适配器和 CTest 接线。
审查期间没有修改生产源码、构建或运行测试/网络负载。

## 当前定向改动的结论

未发现以下新增差异中可由源码直接证明的额外回归：

- ACK 在修改 RTT、largest ACK、flight 和节点所有权前完整校验所有 range，
  并先检查减法边界，避免后部非法 range 导致部分提交或 gap 加法溢出。
- detector/recovery 的有限空节点缓存清空 mapped payload；copy 不复制缓存，
  move 转移缓存；回收不会保留已确认的 shared body/PTO alias。
- 动态 MDS 仅由实际匹配的在途探测 ACK 发布到对应路径；成熟窗口和 flight
  不重置，最低窗口与现有 CA/pacing 参数同步；探测发送或丢失不提前发布。
- 退流通知的 posted callback 在实际派发时通过 RAII 释放；readiness 通知后
  不再访问连接存储；终止 crypto/UDP 错误向外传播并清理未提交批次的恢复状态。

这是当前差异的静态审查结论，不是全部 QUIC 行为无缺陷的保证，也没有穷举
所有回调交错、发送失败模式或多路径行为。完整平台回归及性能结果由协调任务
单独记录；本次只读审查不增加运行验证数量。

## 最终 PMTU fixture 验证的生产路径

`testing/http3/quic_pmtu_cc_server.cpp` 使用真实 `quic_connection`、共享 UDP
入口、timer observer 和 `async_poll_timers()`；它不调用 CC setter 或伪造 ACK。
driver 等待公开发现值达到 4096，代理独立认证解密实际 STREAM 包后定向丢弃，
每轮固定四次 confirmation，最后通过一次 16KiB application write 验证真实
分段、恢复和 late cwnd admission。收到内容逐字节检查并要求 FIN。

永久无生产诊断版本已有匹配的旧版无诊断失败负控；此前诊断对照另证明实际
ordinary loss、旧窗口 floor、empty flight/noPTO 和超窗 pending frame，不能仅
凭一次请求超时推断锁死。测试 peer 显式使用 cryptography AEAD 适配器，未改
site-packages；它不是原版 aioquic 大包互操作认证。独立 netns/loopback 条件及
旧失败、新通过原始证据保留在外部归档：

`C:/Users/25007/.codex/tmp/pmtu-cc-review/phase2-evidence/README.md`

本 fixture 的结论仅涵盖向上 ACK 确认的 MDS 与普通丢包后的 STREAM 进展。
没有宣称覆盖降 MTU、迁移、成功发现后黑洞，以及额外 peer-cap/blocked-probe
场景。独立 loopback 大包测试也不认证物理网络上的禁止 IP 分片策略。

## 发现的既有缺口：纯 DATAGRAM 绕过 cwnd

**尚未修复、尚未验证外部候选补丁。此缺口在 HEAD 中也存在，不是本轮新增回归。**
本轮 STREAM/HTTP3 arena 响应路径不经过纯 DATAGRAM 分支，因此该缺口不影响
当前冻结的 STREAM 性能对照。它仍留在本任务中跟踪，不应据此声称全部 QUIC
正确性已完成。

当前 `quic_connection.cpp` 中可直接追踪的路径：

1. `async_send_datagram`（约 5066 行）将公开 API 请求排队并启动 flush；
   `flush_send_queue`（约 4585 行）调用 `apply_application_datagram`（约 5049 行）。
   这些步骤检查连接状态、协商支持和 frame 大小，没有 cwnd admission。
2. `pack_one_rtt_packet`（约 4345 行）取走 unreliable frame 并设置
   `contains_unreliable_datagram`。当没有其他可靠 frame 或 PMTU probe 时，
   `retransmittable_frames` 为空。
3. 约 4394 行的窗口检查条件只包含可靠 frame 或 PMTU probe，遗漏该 DATAGRAM
   flag，因此可以在 flight 已占满 cwnd 时继续构造和提交纯 DATAGRAM 包。
4. 约 4451 行的 `recovery_tracked` 又包含该 flag，所以这些越窗包仍增加 flight。
   后续 batch wrapper 没有补充窗口检查；pacing 控制速率，也不替代 cwnd 检查。

触发条件是双方协商 DATAGRAM、路径已验证、PMTU 未启用或已稳定，没有其他
可靠 frame 混装，应用在 ACK 延迟/被暂扣时连续调用公开 `async_send_datagram`。
若恰好混装了可靠控制帧，现有窗口检查可能掩盖问题，不代表外层已有保护。

[RFC9221 §5.4](https://www.rfc-editor.org/rfc/rfc9221.html#section-5.4) 要求发送端
遵守 QUIC CC：等待 credit 或直接丢弃未发送 DATAGRAM 都允许，越窗发送不允许。
外部最小候选将 unreliable flag 加入现有 gate；因 frame 已离队，拒绝时选择
合法的本地 drop，可靠 frame 按原方式放回。保留 `!pto_probe` 和 CONNECTION_CLOSE
例外，不给 DATAGRAM 增加重传，也不改变 API 接受请求不等于保证对端收到的语义。

**未应用、未编译、未运行、未作为接受的修复归档**的候选及方案：

- `C:/Users/25007/.codex/tmp/pmtu-cc-review/final-readonly-review/03-datagram-cwnd-admission.patch`
- SHA256：`30f500eaf77c7e87f3fcd184b1f56f3eebe9bb674706b698b06a200435f63b4c`
- 详细调用链及回归方案：同目录 `README.md`。

拟议回归使用真实公开 API、独立认证解密代理和 ACK gate，要求 burst 在首个真实
PTO 到期前结束，避免现有纯 DATAGRAM PTO 退休释放 credit 掩盖结果；随后恢复 ACK
并检查新 marker 进展、旧丢弃标识不重传。另单独检查真正 PTO probe 保留明确的
超窗豁免。该回归尚未实现或执行，须在当前冻结目标实测完成后另行安排窗口。

## 后续 owner 扫描与本地 glibc 诊断限制

针对 system allocator 长跑 RSS 变化，对暂存源码作了定向只读扫描，未找到可证明
的新正常请求 owner 泄漏：heap post 的派发/丢弃均释放，request scope 清理 headers、
QPACK waiter 并退流，共享响应体沿 command/frame/recovery 回收；timer heap 仅存
weak_ptr，expired batch 在休眠前清空，取消通知与 timer 状态随协程完成释放。
这不是全部生命周期或交错均无缺陷的保证，与前述 DATAGRAM 语义缺口分开跟踪。

**未证实 SSL 缓存解释。** 库默认 cache 容量 20480 项、TLS1.3 ticket lifetime 两天、
每握手两个 ticket 只是配置事实。当前完整握手的服务端 ticket 为 stateless；
无 ticket 的 established session 在 `ssl_update_cache` 前判为不可恢复，不能据默认
容量推断当前实际缓存增长。SSL 本体及客户端额外 ticket 引用有对应释放路径。

离线工具检查确认 GDB 17.2-1 可用，glibc 为 `2.44+r24+g16be1518495f-1`，libc BuildID
为 `503200d7fda94a5dc6058d7e0694e5d1dcb2e372`。未安装 glibc-debug，匹配 BuildID 的
本地 debuginfod cache 文件为零字节；禁用 debuginfod 后无法取得 main_arena、mp_、
tcache 或 malloc_state 类型，当前不能可信地按 debug types 直接读 arena/freebins。
未改用手工布局猜测或 mallinfo2 目标函数调用。

本次没有新增测试/负载运行数，没有 attach、下载/安装、调用目标函数或修改生产
源码。RSS 后段平台只能记录为有限观察，既不单独证明泄漏，也不证明无限运行不泄漏。
