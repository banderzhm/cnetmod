# Application 监控闭环：统一执行清单

更新：2026-09-13。本文是后续工作的唯一任务入口；
`observability-implementation-status.md` 只保留历史实验与回归证据，不再作为待办清单。
整体状态：**未完成，禁止以局部测试通过宣称完整 OTEL 或零性能损耗。**

## 1. 固定范围与组织

目标仍是配置 → 装配 → 启动 → 运行监管 → 健康 → traces/metrics/logs →
故障恢复 → 优雅停机 → 真实集成测试 → 平台构建 → 文档与交付。
开关关闭不应改变原始调用结果、取消语义、资源所有权及性能。
不提交、不推送，除非用户重新明确要求。

监控源码统一放在：

```text
src/application/monitoring/
├── core/          # 协议无关的 trace、metric、operation 契约与实现
├── otlp/          # OTLP 编码、确认解析、队列、重试与导出
├── integration/   # HTTP、OpenAI、消息等监控适配器
└── telemetry.*    # Hub：采样、信号出口、资源与关闭协调
```

保留现有模块名 `cnetmod.instrumentation.*` 和 `cnetmod.observability.*`，
本次搬迁不改调用方 API。物理目录归属不等于编译依赖：协议只能依赖中立契约，
不能依赖 Host、Collector 或 OTLP。关闭 HTTP 仍保留 `monitoring/core/`。
`.cppm` 只保留接口和必要模板；核心接口使用英文 `/** ... */` 注释。

## 2. 已有基础，不重复开发

以下只是已完成部分的范围说明，不表示所在大项已验收：

- Builder/Host、具名注册、拓扑启动、回滚、supervisor、健康缓存和管理端点已有实现及部分回归。
- HTTP 和 OpenAI 的关闭路径、错误隔离、上下文、独立指标出口已有回归。
- OpenAI Application 指标已接入 Hub；真实本地 HTTP 接收器验证 token/耗时、本地不重复计数、敏感内容排除。
- OTLP 三信号、有界队列、累计指标/直方图、重试、响应确认、部分接收、丢弃统计和关闭已有测试。
- 专用远程 MySQL 的 4 个 live 用例通过；包括 3 次正常断开、3 次 KILL 自己的连接后替换和租约收尾。不是整个依赖不可达恢复。
- Windows Release、Arch Clang 22 ASAN 的选定用例通过；不等于全目标、所有协议组合或 macOS 已通过。
- 关闭路径已有分配/协程帧比较；历史 HTTP 吞吐/延迟比较仍是 inconclusive，不能算性能达标。

## 3. 执行规则

1. 按下面 T01 → T09 顺序推进，一次只指定一个主任务；必要前置修复挂在该任务下面。
2. 不因看到新问题就跳到另一个组件；新问题登记到对应编号，只有阻断当前任务时立即处理。
3. 完成条件同时包含实现、失败路径、开/关行为、测试和文档，不以“编译通过”单独结项。
4. 每次回显说明：任务编号、改了什么、实际验证了什么、还缺什么；不报缺乏依据的百分比。
5. 下表的“未验收”可能已有实现，含义是缺完整证据；先核实差距，不推倒重写。
6. 当前中断的 `testing/database/outage_proxy.py` 及其测试已落盘，但尚无通过证据。
   暂停它的扩展，T03 再接续；不计入已完成结果。

## T01：目录与依赖边界

- [x] 31 个监控源文件移入 monitoring，迁移前后 SHA-256 相同。
- [x] 修改 CMake HTTP-off 过滤，保留中立契约。
- [x] 更新源码导航并生成 AGENTS.md。
- [x] 验证迁移后的 Windows/Arch 启用监控的选定目标与协议全关核心构建及回归。
- [x] 检查生成的安装/导出模块路径与现行源码导航；历史日志路径保留并注明。

迁移回归：Windows Release 四组测试通过（7.66s），Arch ASAN 四组通过（7.11s）；
两个协议全关配置的 core/test_instrumentation 均重新构建。
31 文件内容 hash 一致。旧的两个目录已无源码；删除空目录的请求被执行环境拦截，
未绕过限制。实际安装后的独立消费者构建属于 T08，而不是此处已完成的清单路径检查。

验收：旧源目录无源码；具名模块依赖检查通过；启用/关闭构建都从新路径生成。
不把旧构建目录中仍存在的历史对象文件算作源目录未搬迁。

## T02：生命周期、资源与取消闭环

- [ ] 逐个服务审计失败 start 后的资源归属：自行清理或报告 cleanup/shutdown_required。
- [ ] 覆盖依赖并行启动、部分失败、原始错误保留、精确逆序回滚、回滚失败后二次清理。
- [ ] 逐个核对维护循环、消费者、帧泵、监听、重连任务归属及失败传播，排除关键失管协程。
- [ ] 核对取消在跨线程投递、锁等待、连接/认证、请求、探测、关闭中的传播与收尾。
- [ ] 验证 required 恢复预算耗尽只触发一次停机；optional 持续降级且不误报就绪。
- [ ] 验证 readiness 先撤销，之后停止接收、排空/取消请求、等待任务、关闭服务、flush。
- [ ] 处理 cleanup_failed 下仍存活任务/租约/外部引用的所有权；不得超时后直接销毁其宿主。
- [ ] 将 deadline 约束与“必须协作取消”的契约写清，验证总预算，不能宣称能强杀任意用户协程。

验收：可控假服务、真实适配器、分配故障和并发 stop 测试覆盖上述状态转换；
所有失败都能定位阶段/组件并留下正确的资源所有权。不得继续靠零散单点测试替代整条状态链。

本轮证据（2026-09-13）：AMQP 0-9-1 start 新增预取消、过期 deadline、两者同时
发生的三个入口测试；修复前全部失败（仍发起连接且返回 connection_refused），
修复后在网络工作前同步返回 operation_canceled / timed_out。
Windows Release 和 Arch ASAN 重建后，完整 test_application_integrations 分别通过
（2.32s / 1.90s）；测试新增文件完成 clang-format。该结果仅覆盖入口拒绝，不能证明
连接中途取消或停机有界，也没有修改独立协议客户端和关闭 OTEL 的热路径。

当前明确缺口，继续在 T02 处理：AMQP 0-9-1 connect 未使用生命周期 token；底层
happy-eyeballs 和 TLS handshake 未贯穿该 token；连接成功后 supervisor 注册失败仍需
确认连接所有权及回滚；async_close 发送关闭帧没有取消预算，帧泵取消/等待也需核实。
下一步先解决该适配器的资源收尾契约和对应失败路径，不将入口测试作为整项完成。

后续进展：协议握手现由无堆分配的作用域回滚对象保护；失败返回或异常展开时
释放 TLS/socket、清空解析缓存并恢复 disconnected，成功才提交。
新增本地 TCP 用例连续两次返回错误的 Connection.Open-Ok，检查原始
unexpected_frame、客户端 disconnected 以及对端 EOF（不是任意读取错误）。
初版测试误把框架 end_of_file 当失败，已按实际异步读取契约纠正；修复前也确实
复现客户端状态未恢复。重建后的完整 Application 集成套件 Windows 通过（2.95s），
Arch ASAN 通过（2.52s）；相关源文件格式检查及 475 模块依赖边界检查通过。
本次没有证明成功重连、分配故障、observer 异常、TLS 握手取消、supervisor 注册失败
和停止预算；这些仍属于 T02，不能以两次失败连接用例冒充完整恢复验收。

连接取消进展：Application 使用生命周期 token + with_deadline；移除并未控制
supervisor 帧泵的独立 pump_cancel_ 分配。协议 DNS/TCP、TLS handshake、握手读写
显式传递 token，提交 open 前再次检查取消。两个本地静默 TCP 对端测试分别检查
deadline 返回 timed_out、主动取消返回 operation_canceled，均要求收到协议头后 EOF
及客户端 disconnected。Windows 完整集成套件通过（3.16s），Arch ASAN 通过（2.68s），
相关格式和模块依赖检查通过。TLS 分支编译成功不计为真实 TLS 取消验证。
仍缺：写锁等待取消、成功握手到 supervisor 注册之间的资源归属、帧泵停止/等待、
异步关闭预算和协议错误到 error_code 的精确映射；新增可选 token 参数的未观测路径
任务帧/CPU 开销也尚未量化，不宣称关闭 OTEL 性能已达标。

注册失败回滚进展：amqp091_service 根据实际连接状态报告 shutdown_required，
从而使失败 start 仍进入 lifecycle 逆序清理。新增脚本 TCP 握手用例，令已停止的
supervisor 拒绝注册帧泵，验证原始 operation_canceled、空的生命周期资源记录、
客户端 disconnected 和对端 EOF。supervisor 的提前停止检查与锁内停止检查统一为
operation_canceled（非法配置仍是 invalid_argument）；MongoDB 对应回归断言同步。
Arch ASAN 的 Application 主套件及集成套件通过（合计 7.82s）；Windows 集成套件
通过（3.13s）。本轮只证明“注册被停止状态拒绝”，不覆盖注册分配失败、活动帧泵
收尾或 optional 服务再次启动时的帧泵注册。

当前阻断完整 T02 验收的新证据：Windows 主套件中的
mysql_host_failed_start_cleans_supervised_pool 在整套运行中未结束；日志显示 optional
启动超时场景进入了运行态并持续恢复。本轮启动的测试进程经确认后被终止，记录失败，
并非自然退出。单独以 15s 测试超时重跑通过（0.09s），因此按时序问题保留，不能算
Windows 主套件通过。下一步检查整体启动 deadline 的完成判定及测试本身的时间假设，
同时为关键生命周期测试加入执行超时，避免测试工具无限等待。

启动预算修复：optional 服务只允许将自己的局部超时降级，不能吞掉来自整体预算的
deadline 取消。子任务比较实际约束 deadline 与整体 deadline，并保留取消原因；
每层 join 后再次检查整体期限。新增假服务直接注入 deadline 原因，分别验证组件预算
允许降级、整体预算必须失败，不依赖两个定时器回调的先后顺序。
Windows 主套件与集成套件固定重复三轮均通过（合计 28.19s），包含之前未结束的
MySQL 测试。Application 测试统一配置 60s CTest 超时（live 专项更短的限制仍保留）。
这是已识别预算竞态的修复证据，不宣称所有平台时序问题已排除。
Arch ASAN 两套测试也固定重复三轮通过（23.84s）；模块依赖检查通过。
下一步回到 AMQP 活动帧泵的停止/等待和关闭 deadline；预算案例作为持续回归保留。

帧泵子任务进展：移除 async_run 内部 detached heartbeat；接收与心跳通过 when_all
拥有和等待，各用独立取消 token，任一侧失败取消另一侧。传输在子任务结束之后
由作用域清理，避免读取已返回 disconnected 而 socket 仍未关闭。无心跳配置不创建
心跳任务。新增 lifecycle 正常启动/停止用例，协商默认心跳后验证 supervisor running、
停止结束、对端 EOF，以及连接引用计数只剩 client 和测试观察值（无睡眠心跳持有）。
Windows/Arch ASAN 完整集成套件通过（3.17s / 2.71s），相关格式及依赖检查通过。
仍需验证：心跳写锁/写入阻塞、心跳异常和分配失败、async_recover 中另行 spawn 的读取
任务归属、直接 async_close 的取消预算、pending RPC/confirm 的终止通知和再次启动。
协议帧泵协程结构已变化，禁用监控性能与分配基线须按 T07 实测，不能声称无开销变化。

pending RPC 收尾进展：帧泵退出的传输清理逐项取出 pending RPC，锁外完成
connection_closed，不分配临时 vector 或诊断文本。生命周期停止测试现在先等待
TCP 对端解析到 Channel.Open，并确认该调用仍挂起，随后停止并验证调用收到明确
终止错误、对端 EOF 和无后台连接引用。Windows/Arch ASAN 集成套件通过
（3.18s / 2.71s）；相关格式和模块依赖检查通过。
该用例不覆盖并发跨执行器注册 RPC；目前未改变单连接执行器归属契约。
下一步处理发布确认：fail_all 临时集合分配、observer 抛异常导致后续通知遗漏，
reserve_sequence noexcept 内 set::insert 可能分配失败，以及帧泵退出时确认表未统一清理。
随后仍需完成 async_recover 读取任务归属、关闭 deadline 和写锁等待取消。

确认 tracker 进展：观察者注册以只读快照发布，fail_all 不再分配临时 vector，
清空确认集合后锁外通知，异常不阻断后续失败通知；settle 通知所有仍存活的观察者后
重新抛出首个异常，保留正常确认路径的异常传播。reserve_sequence 移除错误 noexcept，
成功插入后才递增，避免分配失败导致 terminate 或跳号。测试覆盖两位观察者中首位
抛错、后续仍获通知、回调重入注册仅对下一轮生效及 pending 清空。
Windows/Arch ASAN 集成套件通过（3.19s / 2.71s），协议 skill 和 AGENTS 已同步。
尚未完成分配失败注入、async_publish 对资源异常的 error-code 边界以及帧泵退出时
对全部确认 tracker 的统一终止通知；注册快照的成本变化也需纳入 T07，不能视为性能验收。

确认连接收尾进展：帧泵退出时逐项移除确认 tracker、锁外通知 connection_closed；
服务器主动 Close 路径也移除已通知的 tracker，避免退出清理重复处理。
现有生命周期 TCP 用例扩展为打开通道、启用 confirms、发布但不 ACK，并保持另一个
Channel.Open RPC 挂起；确认对端实际接收发布及 RPC 后才停机，验证发布失败通知一次、
RPC 结束、对端 EOF 和无心跳引用。Windows/Arch ASAN 集成套件通过（3.18s / 2.77s）。
该结果不覆盖真实 broker 中断、主动 Connection.Close 原因保留、跨线程注册、分配
故障以及无帧泵的直接关闭路径。后续优先补分配故障和 async_publish 资源错误边界，
然后继续恢复读取任务与关闭预算。协议独立于 Application 的模块依赖检查仍通过。

tracker 分配故障证据：在现有 test_http_disabled_overhead 的线程局部分配注入器中
新增三个用例。序号节点分配失败抛 bad_alloc、pending 为 0，恢复后的序号仍为 1；
按分配位置逐个注入注册失败直到首次成功，失败时保留原观察者且不发布候选观察者；
禁止下一次分配时 fail_all 仍清空 pending 并完成通知，注入点保持未被消耗。
Windows/Arch ASAN 完整分配测试套件通过（3.65s / 3.82s），新增文件格式检查通过。
仅测试程序替换分配器；本轮未修改生产实现。这些证据只覆盖 tracker，不覆盖用户
回调自行分配、完整 async_publish、持续 OOM 或 CPU 性能。下一步仍是发布错误边界。

发布入口进展：is_open 同时检查连接状态；关闭后的发布在编码和确认序号分配前
返回 channel_closed。exchange/routing_key 超过 255 字节返回 invalid_field，避免
short-string 长度截断。TCP 生命周期用例验证超长 routing_key 拒绝后合法发布仍为
序号 1，停机后再次发布被拒绝且不重复发送失败通知。Windows/Arch ASAN 集成套件
通过（3.19s / 2.77s）。
发送中途失败尚未修复：async_send_message 当前先发方法帧，再编码内容头/消息体，
且写锁按帧获取；资源失败可能发生在部分报文已写出之后，同通道并发消息也需验证
帧序列不交错。必须明确提交边界、失败后连接可用性和确认序号归属，不能只 catch
bad_alloc 后继续复用连接。重连后旧 channel 的有效性也需要会话代际校验。

消息写入序列进展：async_send_message 在完整方法/内容头/消息体期间持有一次
连接写锁；内部编译期免重复加锁路径复用现有编码与写入，不新增运行时锁模式开关。
获得锁后重新检查连接状态。TCP 用例协商 4096 字节帧，并发发送两条 16KB 消息，
对端检查方法/内容头/分片体状态顺序，最终完整收到三条消息，再验证停机清理。
Windows/Arch ASAN 集成套件通过（3.18s / 2.76s）。
仍缺部分发送后的异常恢复、确认序号提交边界、写锁取消、连接代际及多通道公平性；
一次持锁覆盖整条消息影响并发调度，T07 必须测量，不将局部通过算作性能达标。

发布预编码进展：方法帧和内容头在首次写入前完成编码，确认序号在编码成功后、
持有连接写锁时分配。新增超长消息头字段失败后再次合法发布的真实 TCP 回归，
验证无残留方法帧且确认序号仍从 1 开始。Windows 构建及 application_integrations
通过（3.23 秒），Arch ASAN 构建及同套件通过（2.78 秒）；四个相关文件格式检查、
475 个模块依赖检查及 AGENTS 同步检查通过。发送部分内容后的分配/I/O 失败、
连接失效处置和确认记录收尾仍待完成；本结果不是完整发布故障验收。

协商帧上限进展：发布前比较方法帧和内容头的完整 wire 长度与 frame_max；
超限以 frame_too_large 拒绝，不分配确认序号、不发送部分消息。新增 TCP 用例在
修复前确定失败：超大消息头被接受、下一条确认序号变成 2、对端多收一条消息。
修复后 Windows 集成套件通过（3.17 秒）、Arch ASAN 同套件通过（2.77 秒），
两端目标构建成功。此检查不解决已发送部分消息后的 I/O/分配失败；下一步仍需
处理该失败路径的连接失效、活动读写收尾和确认通知，不能直接销毁仍被读协程使用的 TLS 对象。

部分发布中断进展：发布写锁作用域内新增失败保护，异常或错误返回时中断 socket，
阻止继续发布，但不销毁活动读写借用的 TLS 对象。帧泵先收尾读取/心跳子任务，
再等待写锁后释放 SSL/socket，锁外清理 RPC 和确认记录。传输中断状态与正常 Close
握手区分，避免监管器取消与 Close 写入竞争导致停机误报。新 TCP 回归在第 4 条
8 MiB 消息的内容头到达后关闭对端，验证发布失败、后续发布拒绝、立即 lifecycle.stop
成功、确认失败通知一次、资源归属清空。中途曾分别在 Windows/Arch 复现停机误报；
最终用例没有添加等待来绕过该竞态。

验证：Windows 和 Arch ASAN 目标构建成功，集成套件各连续 3 次通过（Windows
3.21/3.22/3.19 秒，Arch 首两次 2.77/2.75 秒，完整结果见 CTest 日志）。
此结果覆盖明文 TCP 断连，不证明真实 TLS 并发销毁安全、分配失败全矩阵、无帧泵
直接关闭、老 channel 重连代际隔离或所有超时边界完成。新增帧泵子任务有每次 run
的协程成本，尚未性能验收，不据此宣称关闭监控零开销。

通道代际进展：每次连接尝试递增无回绕代际，channel 捕获创建代际；is_open 和所有
channel 异步操作入口拒绝旧代际，publish 在获取连接写锁后再次校验。新 TCP 用例
以同一个 client 建立两次成功会话，确认旧 channel 的 publish/ack 返回 channel_closed
且不写入第二个连接，新 channel 可正常打开。Windows 构建和集成套件通过（3.19 秒），
Arch ASAN 构建和同套件通过（2.76 秒）。
该入口防护不等于完整重连隔离：其他 RPC/非发布写入在排队后的再次校验、挂起 RPC
恢复期间的归属、consumer handler 和确认 tracker 清理、连接重建与活动读写互斥仍须完成。
这是连接安全检查，不引入 OTEL 反向依赖；新增检查的性能尚待 T07 测量。

重连准入与排队写进展：async_connect 在替换传输前检查 disconnected/recovering 状态、
取得读取所有权，并以 try_lock 拒绝仍有活动写入的连接；不在不可取消写锁上等待。
拒绝重连不修改旧 channel/挂起 RPC。普通帧和两种 RPC 写路径在取得写锁后检查
代际及 open 状态，发送失败只删除自己注册的 pending 对象。TCP 用例覆盖已有会话
和挂起 channel-open RPC 时再次 connect 被拒绝；部分发布断连用例并行提交 ACK/QoS
RPC，验证排队操作失败返回且停机回收完成。Windows 构建和最终集成套件通过（3.17 秒）。
Arch ASAN 最终目标已重建且同套件通过（2.77 秒）。还需针对 RPC 注册后的分配/编码异常增加作用域回滚，
并验证自动恢复任务、consumer/tracker 残留状态及真正跨线程误用边界；当前约定仍为单执行器。

RPC 登记异常回滚进展：登记成功后立即建立无额外分配的作用域守卫；正常结果、
错误返回和异常展开均按 pending 对象身份清理，避免残留登记或误删替代请求。
新增独立分配故障测试使用现有 test-only new 注入器，从 QoS RPC 启动起逐位置注入，
直到首次 I/O 挂起前的分配全部成功；每次故障后使用同一 channel 正常调用 QoS，
验证没有遗留 command_invalid 登记。Windows 定向测试通过，完整 integrations 与
disabled_overhead 套件通过（3.22/3.68 秒），Arch ASAN 对应目标构建和套件通过（2.79/3.86 秒）。
范围限制：未覆盖首次挂起后的分配、部分 RPC 帧写入、真实 TLS、所有恢复状态；
disabled_overhead 测试通过也不等于全协议无性能回归，性能门禁仍在 T07。

预连接服务监管进展：amqp091_service::start 不再仅凭 connection=open 直接成功；
已有活动监管任务才走幂等返回，任务缺失/终止时补注册帧泵，登记失败仍返回原错误并
由生命周期回滚连接。新增预连接正常启动、预连接但监管器已停止两项 TCP 回归；
Windows 构建和集成套件通过（3.22 秒），Arch ASAN 构建和同套件通过（2.84 秒）。
该检查只在 Application 启动入口执行。

Application 恢复所有权进展：帧泵使用零重试预算，仅监管一次会话，不自行重启或
发出 required 耗尽通知；故障保留在任务状态中，由 health/lifecycle 统一派发服务
恢复任务、重建连接、登记新帧泵及管理故障周期预算。服务的 required/optional 身份
不变。probe 同时检查连接和帧泵状态，缺失/失败的帧泵不会因 socket=open 而误报 up。
新增 required/optional TCP 回归：首会话断连，健康缓存撤销 readiness，经 reconcile
触发第二次连接，连续两次健康确认后恢复 readiness，最后停机清空资源；帧泵故障
不单独提前通知应用退出。Windows 最终构建和集成套件通过（3.26 秒），Arch ASAN
最终构建和同套件通过（2.83 秒）。
预算耗尽回归：首次 TCP 会话断开后关闭监听，后续连接持续失败；设置 50ms 恢复预算，
验证 required 只通知一次耗尽（重复 reconcile 不重复通知），optional 不发出耗尽通知，
两者 live 保持真、ready 保持假，显式停机清空资源，required 的终态错误仍由 stop 返回。
Windows 构建和集成套件通过（3.46 秒），Arch ASAN 构建和同套件通过（2.95 秒）。
这是 lifecycle/supervisor 的实际连接失败测试，
不代替 Host 进程退出、真实 broker 拓扑恢复或长期抖动验收。
仍需验证真实 broker 的拓扑/消费者恢复及持续异常矩阵；协议独立
async_recover 中自行 spawn 的 reader 也仍须改成明确可取消、可等待的所有权。

独立恢复退避进展：async_recover 提前取消立即返回；拒绝 open 会话及读取所有权
尚未释放时的恢复请求。退避从不可取消 async_sleep 改为传入调用者令牌的定时器，
取消/定时器失败时退出恢复状态，不等待完整退避。新增 30 秒退避被取消后两秒内
收尾、提前取消同步结束、打开会话拒绝恢复且旧 channel 保持有效的回归。
Windows 最终构建和集成套件通过（3.50 秒），Arch ASAN 构建和同套件通过（2.96 秒）；
Windows 曾遇到临时 LNK1104/C1083，重试成功。
独立恢复互斥进展：作用域 recovery_active 覆盖退避、握手及拓扑恢复；外部 connect
和第二轮 recover 被拒绝。内部握手采用私有模板分支，公开 connect 直接返回任务，
没有额外包装协程。退避取消测试同时验证竞争 connect/recover 同步拒绝，原恢复状态
不变且仍能取消退出。Windows 构建及集成套件通过（3.49 秒），Arch ASAN 构建及
同套件通过（2.95 秒）。仍是单执行器契约。
close 恢复收尾进展：恢复作用域登记调用者取消令牌及协程等待组，退出时移除令牌并
通知完成；close 先取消恢复和已登记 reader，再等待恢复作用域退出，之后处理传输。
新增 close 中断 30 秒退避的回归，验证两秒内恢复返回 cancelled、close 成功且状态
disconnected。Windows 构建/集成套件通过（3.50 秒），Arch ASAN 通过（2.96 秒）。
多关闭者回归进展：同一退避恢复期间并发启动两个 close，验证两者都完成且返回成功。
reader 生命周期修复：原派发使用临时捕获 lambda 协程，存在闭包销毁后访问捕获成员
的风险；改为无捕获协程，将 shared connection/token 按值存入协程帧。新增实际第二次
握手后的独立恢复运行测试，关闭后继续驱动事件循环，验证 reader 释放连接引用。
Windows 构建与集成套件通过（3.56 秒），Arch ASAN 构建及同套件通过（2.96 秒）。
后续收尾：reader 已改用共享完成票据登记，close 等待其释放连接引用；引用回归已去掉
close 返回后的额外等待，Windows 72 个用例（3.52 秒）和 Arch ASAN 套件（3.01 秒）通过。
新增协议失败保留：reader 返回的失败不再丢弃，主动取消不作为后台故障；新连接清除旧
完成记录。脚本 TCP 对端发送截断 Basic.Cancel，验证 close 返回原始 malformed_frame
及消息、无额外连接引用，并验证后续重新连接关闭不继承旧错误。
该扩展回归 Windows 73 个用例通过（3.56 秒），Arch ASAN 同套件通过（3.00 秒）；
受影响源码格式检查、475 模块依赖方向检查和 AGENTS.md 同步检查通过。
新增立即关闭再连接回归：恢复 reader 派发后不等待定时器，直接关闭并再次握手。
该用例在修复前 Windows 失败：旧会话 ready 队列残留 Channel.OpenOk，污染下一次握手。
新握手取得读取所有权并确认写锁空闲后，清空 parser/ready，释放旧 TLS/socket，
再建立新传输。修复后 Windows 74 个用例通过（3.59 秒）。此用例使用明文 TCP，
不能作为真实 TLS 资源清理的验证证据。Arch ASAN 同套件通过（2.97 秒），
受影响文件格式、模块依赖方向及生成文档同步检查通过。
派发失败回滚：新增握手完成后连续 7 个分配位置的失败注入，修复前 Windows 出现
11 个断言失败（异常后仍为 open、posting 失败返回恢复成功、close 后仍非 disconnected）。
握手至 reader 派发之间增加无挂起的作用域回滚；同步 posting 错误从完成记录重新抛出，
先释放 socket/TLS/parser/ready 并置 disconnected。回归验证异常、状态、引用释放和对端 EOF。
Windows Application/分配套件通过（3.59/3.78 秒），Arch ASAN 同套件通过（3.04/4.25 秒）。
核心实现及新 fixture 格式检查通过；旧 test_http_disabled_overhead.cpp 仍有格式诊断，
未将这一局部验证记为全量格式通过。
运行异常的完整资源清理矩阵、真实 TLS、握手/拓扑阶段的 close 竞争仍须专项测试。
拓扑取消进展：将恢复令牌与内部 reader 令牌以作用域注册关联，拓扑 RPC 前等待 reader
取得读取所有权，避免首个 RPC 落入无令牌的独立读取。取消造成的 RPC 失败返回 cancelled。
脚本 TCP 对端不回复恢复 Channel.Open，收到请求后取消：修复前 Windows CTest 60 秒
超时，修复后 Application/分配套件通过（3.60/3.77 秒）。尚未覆盖 exchange/queue/bind/consume
各阶段、快照部分恢复失败和关闭交叉。
启动等待已由 1ms 定时检查改为显式等待组通知：取得 reader 所有权后通知，提前返回和
派发/执行异常也释放启动等待者。普通 async_run 非协程转发到编译期空回调实例，
不添加动态观察者查找；具体任务帧大小及 CPU 性能仍须 T07 测量。
此版 Windows Application 通过（3.63 秒）、AMQP 分配专项通过（0.14 秒）；Arch ASAN
Application/全分配套件通过（3.00/3.83 秒）。Windows 全分配套件一次以 0xc0000409 退出，
最后输出为 mongodb_owned_timeout；该组单独运行通过（0.05 秒），根因未定位，必须保留
为 T02/T08 未决故障，不能以专项通过替代完整门禁。
同一二进制随后一次完整诊断运行通过（3.79 秒）；此结果说明暂未稳定复现，不撤销上述故障。
崩溃定位改进：测试开始标记显式 flush，避免最后一行被缓冲截断而误定位用例；
MongoDB 握手/超时/移交截止后 terminate 前增加断言，保留具体未完成任务的源码位置。
固定 10 次 Windows 全分配套件 until-fail 诊断均通过（共 37.98 秒），不作为根因修复证据。
测试过滤零匹配现在返回退出码 1，Windows 不存在过滤条件的负例已验证，避免空跑假绿。
最终诊断版本全分配套件 Windows/Arch ASAN 分别通过（3.82/3.81 秒）。
拓扑记录回滚：扩展静默 Channel.Open 取消用例，首次连接声明交换机、取消第一次恢复、
随后再次恢复并检查对端线帧。修复前仅收到 1 次声明而非 2 次，证明原始记录被 clear 丢失。
恢复改在临时记录器中重建，作用域守卫在失败/异常/取消时恢复原记录，完成后保留新记录；
回滚不分配、不挂起，只保护客户端恢复记录，不撤销 broker 已执行的操作。
Windows Application/分配套件通过（3.60/3.79 秒）。仍须验证 queue/bind/consume、
部分成功后的恢复失败，以及恢复期间外部并发修改的准入规则。
Arch ASAN 同两套件通过（3.07/3.83 秒），受影响源码格式、475 模块依赖方向与文档同步通过。
分阶段取消矩阵：首次连接建立 exchange/queue/binding/consumer，分别在 Channel.Open、
exchange、queue、binding、consumer 的回复前取消恢复；再次恢复检查四种请求的完整顺序，
并检查取消轮仅发出到指定阶段的前缀，覆盖已完成部分操作后的回滚。Windows Application
76 个用例通过（3.78 秒）。此测试不验证请求参数、服务端命名队列重映射、broker 拒绝或真实 broker。
Arch ASAN 同套件通过（3.07 秒）；fixture 格式、475 模块依赖方向及生成文档同步检查通过。
拓扑参数与队列重映射：五阶段取消矩阵改为声明空队列名，脚本对端逐会话返回 a/b/c；
检查请求仍声明空名，绑定与消费者使用当前会话生成名，同时保留交换机名及 direct 类型、
orders.created 路由键和 c 消费者标签。Windows 76 个用例通过（3.79 秒）。
此证据仅覆盖上述字段，尚未覆盖完整字段表/标志位、真实 broker 拒绝和消费者实际投递。
Arch ASAN 同套件通过（3.12 秒）；本轮 fixture 格式和生成文档同步检查通过。
服务端拒绝矩阵：脚本对端分别在 Channel.Open/exchange/queue/binding/consumer 返回
Channel.Close 406，验证恢复结果保留 precondition_failed、406、bad 文本及原请求 class/method，
调用者取消令牌未被伪装为取消，并在随后完整恢复中验证记录顺序及队列名映射。
Windows Application 77 个用例通过（4.00 秒）。这只验证脚本协议对端，不替代实际 RabbitMQ 验收。
Arch ASAN 同套件通过（3.13 秒）；fixture 格式及生成文档同步检查通过。
恢复后投递：五阶段取消/五阶段拒绝矩阵最终恢复收到脚本 Basic.Deliver、content header
和两段 body。验证原消费者回调恰好一次、consumer_tag c、delivery_tag 7、redelivered，
交换机/路由、text/plain 及重组后的 ok 内容；Windows Application 77 个用例通过（4.17 秒）。
这仍是脚本 TCP 端到端投递，不代表真实 broker 持久化、ACK、重投语义已经验收。
Arch ASAN 同套件通过（3.20 秒）；fixture 格式及生成文档同步检查通过。
恢复建通道准入：公开 async_open_channel 在 recovery_active 时返回 command_invalid，
非 open 会话返回 connection_closed，均在分配通道编号前拒绝；恢复内部走私有编译期分支。
五阶段取消/拒绝矩阵在恢复 Channel.Open 挂起时执行外部建通道，验证拒绝及下一轮编号连续。
Windows Application/分配套件通过（4.18/3.78 秒）。此限制不等于全部配置/观察者变更已冻结，
同连接仍遵循单执行器契约；普通调用的 CPU/任务帧开销须 T07 单独验证。
Arch ASAN 同两套件通过（3.27/3.83 秒）；受影响源码格式、475 模块依赖方向及文档同步通过。
通道上限回绕修复：原实现 next_channel++ 后才检查协商上限，反复拒绝最终会回绕。
改为校验后递增，零保留为耗尽哨兵。脚本首连接协商 channel_max=1，已开通道后连续
65,536 次建通道均须 invalid_channel，旧通道仍 open；后续连接正常使用未污染的编号。
Windows Application/分配套件通过（4.24/3.80 秒）；未将该测试等同于实际创建满 65,535 个通道。
Arch ASAN 同套件通过（3.34/3.81 秒）；受影响源码格式、475 模块依赖方向及文档同步通过。
单会话通道编号：新握手清理旧 RPC/确认、投递处理器和内容组装状态，再将 next_channel
设为 1，避免重连累计耗尽协商上限。代数检查继续拒绝同编号的旧通道。
拓扑矩阵每轮均协商 channel_max=1，验证新连接 Channel.Open 使用 1，旧句柄无效，
恢复和投递仍正常；Windows Application/分配套件通过（4.19/3.79 秒）。历史“编号连续”
仅代表当时未被拒绝请求污染的证据，当前正确语义为每个会话重新分配。
Arch ASAN 同两套件通过（3.32/3.80 秒）；受影响源码格式、475 模块依赖方向及文档同步通过。
历史 Application 集成审计（后续接入已修复，见“当前下一步”）：start 当时只 connect + supervise(async_run)，生命周期
重连并未调用拓扑重放。因此底层独立恢复矩阵通过不代表 Application 订阅恢复完成。
已将重放提取为私有 async_replay_topology(saved, token)，只负责记录事务和协议操作，
不拥有连接、帧泵或重试预算；原独立恢复入口复用它。该恢复路径多一个协程帧，普通请求不调用它。
下一项明确主线：提供受监管会话的重放接入，由同一任务所有者管理 reader + replay；
启动通知须在重放成功后发布，readiness 不得因仅握手成功而恢复；取消/截止必须收尾两者，
重试预算仍只由 lifecycle 控制。不得直接叠加独立 async_recover 的重试循环和内部帧泵。
Windows Application/分配套件通过（4.28/3.78 秒）；本项 Application 连接缺口仍未完成。
Arch ASAN 同两套件通过（3.76/3.81 秒），受影响源码格式及 475 模块依赖方向通过。
单会话入口：新增 protocol_connection::async_run_session(token, on_ready)，在已连接传输上
以 when_all 拥有 reader + replay，调用者取消转发给 reader；重放成功发布 ready，失败取消
并等待 reader。无连接重试循环，不调用内部 detached 恢复 reader。普通 async_run 保持原入口。
新增先经历一次消费者拒绝、再由此入口重放/接收分段投递/取消并等待退出的回归；Windows
Application 78 个用例/分配套件通过（4.29/3.79 秒）。新入口自己的完整分配失败与 ready 延迟
门禁当时尚未测试，amqp091_service 尚未调用它；服务启动屏障和 probe 的后续证据见“当前下一步”。
Arch ASAN 同两套件通过（3.30/3.78 秒）；受影响源码格式、475 模块依赖方向及文档同步通过。
Arch ASAN Application/分配套件通过（3.02/3.84 秒）；受影响源码格式、475 模块依赖
方向及生成文档同步检查通过。
也未完成该分支的分配/性能及真实重连拓扑验收。

## T03：真实依赖中断与恢复

依赖部署优先使用本机 WSL Arch；需要远程环境时可通过宝塔安装（用户已授权）。
远程操作遵循对应 skill，使用专用测试实例、账号与端口；不改动现有业务服务。
此授权不改变 T02 → T03 的执行顺序。

- [ ] 完成并验证 loopback 故障代理：只影响测试连接，切断确认前等待 relay 收尾，恢复可重复。
- [ ] 真实 MySQL：启动 ready → 切断测试网络 → ready=503/live=200 → 恢复网络 → 连续健康确认 → ready=200。
- [ ] 真实 MySQL：保持中断直至 required 预算耗尽 → Host 失败返回 → 所有任务与连接收尾。
- [ ] optional MySQL：预算耗尽仍存活、持续降级、后续恢复周期可重新就绪。
- [ ] Redis、PostgreSQL、MongoDB 逐个执行同一故障矩阵，包含认证失败、池满、租约未归还。
- [ ] Kafka、MQTT、AMQP 0-9-1、AMQP 1.0 验证 broker 中断、重连、消费恢复和停机。
- [ ] OpenAI 本地兼容服务及 gRPC 进程内服务执行超时/断流/取消/恢复测试。

验收：真实服务/容器的可复现输出，HTTP 管理端点状态和最终资源计数相互印证。
使用专用账号/数据库；不停止共享 MySQL、不修改业务数据、不切断其他用户连接。
SSH 操作使用 ssh-skill；凭据不进入仓库、日志、进程参数和报告。

本地故障代理基座（2026-09-13）：`testing/database/outage_proxy.py` 现仅监听
loopback，`down` 在应答前取消并等待 relay，`up` 可重复恢复。首次 Windows 测试揭露
`close()` 先等待 listener、后取消 relay 的死锁：`asyncio.Server.wait_closed()` 会等待
持有客户端连接的 handler，因而永远到不了取消步骤。关闭现按“停止接收 → 取消/等待
control 与 relay → 等 listener”执行；半开控制连接、已有 relay 的切断/收尾、恢复和
无效控制请求三项单元测试均通过（0.071s）。测试客户端 close 也有 2 秒上限，避免
Windows proactor 的对端已关闭连接让 teardown 无限等待。该基座完成，不等价于真实
MySQL 状态矩阵。宝塔 API 的现有 token 返回 `status=false`；远程 socket root MySQL
需要密码，且没有安全的 client credential 文件。未读取面板密码库、未创建或修改任何
远程数据库；继续真实验证前需要有效宝塔 token 或专用 MySQL 管理凭据。

## T04：各组件自动观测覆盖

每行都必须完成：自动装配入口 + trace/metric + 日志关联 + 错误/取消隔离 +
禁用路径 + 真实调用测试。表中项目均为剩余覆盖/验收，不表示全部缺实现。

| 组件 | 必须补齐或核实的范围 |
|---|---|
| HTTP server | route span、耗时/错误、异常和取消、流式/SSE 结束、请求日志关联、管理/业务端口隔离 |
| HTTP client | 普通/流式/批量入口、重定向/重试范围、传播、取消、状态码、HTTP 版本与 TLS 组合 |
| OpenAI/Agent | Chat/Responses/Embedding 等入口、流式、模型/工具/检索/Agent 父子关系、token/成本/重试/拒绝、完整指标出口 |
| Redis | 普通命令、request-builder、pipeline、池借用入口自动注入、服务错误、重连、指标 |
| MySQL | 直连/池/ORM、query/prepared statement、事务、错误类别、指标、敏感 SQL 参数排除 |
| PostgreSQL | 直连/池/ORM、prepared statement、事务、预热/重连、指标及错误保留 |
| MongoDB | 命令/游标/池、操作 span 与指标、hello/auth/PING/关闭取消边界 |
| Kafka | producer、consumer processing、批量链接、重试、事务/offset/flush、消费任务归属、指标 |
| MQTT | publish/receive、v3/v5 能力区别、v5 传播、重连/会话/QoS 完成边界、指标 |
| AMQP 0-9-1 | publish/consume、传播、confirm/ack/nack/requeue、channel/帧泵和恢复、指标 |
| AMQP 1.0 | send/receive、传播、delivery settlement、link/session 恢复、指标 |
| gRPC client/server | unary 与流式、metadata 传播、deadline/cancel/status、服务路由和日志/指标 |

验收：每行有明确的测试入口和真实执行结果。现有协议互操作测试不能直接替代监控验收；
“已注册 service”不等于“用户从 service 取出的 client 已自动观测”。

## T05：统一监控能力与可靠投递

- [ ] 中立 span 数据模型：事件、链接、批量/fan-out；消除仍依赖 HTTP 特有字段的通用语义。
- [ ] 采样与上下文：root/parent、未采样传播、非法头处理、跨协程/线程、重试和流式边界。
- [ ] 独立 traces/metrics/logs 开关组合覆盖所有生产者；关闭时不构造载荷/时间戳/属性。
- [x] 业务日志接入统一日志出口并关联显式 trace/span，不使用 thread-local 活跃 span。
- [ ] 指标类型、单位、累计周期、直方图边界/溢出、标签基数上限和本地/OTLP 一致性。
- [ ] 导出请求体/批次字节上限、定时采集策略、公平性；不能仅靠队列条数限制总体内存。
- [ ] 重试、部分接收、网络慢/断开、持续队列溢出和丢弃计数的组合故障测试。
- [ ] 多信号 flush/shutdown、残留 worker 收尾、重复关闭及移动/销毁契约的完整验证。
- [ ] 提供可运行的调用链查看方案和示例配置：Collector、trace backend、指标、日志关联。

验收：真实测试接收器检查 wire 数据与统计一致；后端可看到完整示例调用链。
已有三信号接收用例只是基础，不等于所有组件均已完成此验收。

业务日志增量（2026-09-14）：核心 Logger 增加完成事件 observer 与
`logger::log(level, log_correlation, message)`；关联值由调用方显式传入，core 不导入
Application/OTEL，也不维护 thread-local active span。仅当 OTLP logs 已启用且
`capture_framework_logs` 为 true 时，Telemetry Hub 才注册 observer；关闭或析构时移除它，
默认 Logger 路径不产生 observer/OTLP 开销。Windows Release 和 Arch clang++ 22 ASAN 的
`test_log`、`test_otlp_exporter` 通过，依赖方向检查通过。仍需在 T05 的后端端到端示例中展示
该关联日志与 trace 的联合查询。

## T06：配置、安全与管理面

- [ ] 对全部服务完成默认值 < JSON < 环境变量 < builder 优先级和 enabled 显式装配矩阵。
- [ ] 未知字段、类型、凭据、端口、容量、超时/溢出与协议依赖在 build 阶段统一校验。
- [ ] ${ENV_VAR}、连接串、错误消息、HTTP URL、SQL、模型输入输出、消息载荷和日志统一脱敏审计。
- [ ] 热更新只允许日志级别/采样/健康周期/恢复策略；其他变化明确要求重启。
- [ ] 热更新失败不半提交；并发查询可见性契约、采样与恢复生效范围有回归。
- [ ] live/ready/health/prometheus 输出、缓存探测、阈值恢复、管理端口安全默认值与停机行为完整验证。

验收：配置及管理接口正反用例，秘密标记不出现在任何响应、日志和 OTLP 属性中。

## T07：关闭性能与启用开销

- [ ] 为各组件确认真正未观测的历史基线，记录源码/二进制/编译器/分配器/配置身份。
- [ ] 逐入口比较禁用路径的任务帧、分配次数/字节、请求字节和结果，不只测 HTTP。
- [ ] 排除关闭路径的时间读取、ID 生成、属性复制、注册查找、锁、后台唤醒和队列构造。
- [ ] 预先固定负载、进程样本数、CPU/并发/预热与分析方法，测吞吐、p50/p99、CPU、内存。
- [ ] 对已有 HTTP inconclusive 结果补齐证据；不反复跑到一次通过，不自行放宽零回归要求。
- [ ] 测启用后的资源上限与开销；Collector 不可用/队列满也不能阻塞业务。

验收：原始样本、环境与分析结果可复现；结论只覆盖实际测量范围。
分配相同不等于 CPU 相同，局部通过不等于全框架性能无变化。

## T08：构建与 CI 矩阵

- [ ] Windows、Linux、macOS 的模块构建、测试、格式检查全部运行成功。
- [ ] 协议全开、全关、常用子集，包括 HTTP-off、ORM-off、SSL-off 和相关依赖组合。
- [ ] 安装/打包后的独立消费者模块构建，不只检查生成的安装清单。
- [ ] Linux 真实 Redis/MySQL/PostgreSQL/MongoDB/Kafka/Mosquitto/RabbitMQ/AMQP 1.0 容器门禁执行成功。
- [ ] OpenAI 兼容服务、gRPC 进程内服务、OTLP 接收器及中断恢复纳入门禁。
- [ ] 检查缺服务/缺凭据/超时/用例没执行不能伪装绿色；检查新代理自身的测试。
- [ ] 按风险完成 ASAN 与可用并发检测；区分工具限制、跳过和真正通过。

验收：实际构建/CTest/CI 日志，而不是只有 YAML 文件或工具配置。
当前 macOS 和远端 CI 的完整通过证据缺失；提交/推送前不能宣称已通过。

## T09：文档与交付

- [ ] 完整示例覆盖 HTTP → OpenAI/数据库/消息/gRPC 和开关关闭模式。
- [ ] 配置模板、迁移说明、指标名称/单位、日志和调用链查看教程同步。
- [ ] application/observability 及受影响协议 skill 更新，再生成/检查 AGENTS.md。
- [ ] 核心英文文档注释、接口/实现分离、命名与 clang-format 全量检查。
- [ ] 审阅全部未提交变更，区分本任务、用户已有修改与实验产物；不夹带凭据和 benchmark 临时文件。
- [ ] 按 T01～T08 逐项核对证据后给出最终验收报告；收到提交授权才提交/推送。

## 当前下一步

本轮 Application AMQP 会话接入已通过 Windows 两组回归：启动等待拓扑重放，
probe 不再仅检查 open socket；required/optional 恢复测试扣住交换机确认，断言
恢复任务仍运行且健康为 down，确认后经健康阈值恢复 readiness。
此证据仅覆盖脚本 TCP 交换机恢复；完整订阅、真实 RabbitMQ、启动取消/派发故障矩阵
仍属于 T02/T03 待验收项，不能标记整项完成。
随后补齐 required/optional 的重放等待停机回归：不回复交换机确认，调用 lifecycle.stop
并等待 supervisor.join；断言两个任务 stopped、连接关闭、登记清空、没有错误升级。
Windows Application 套件通过（4.37 秒），Arch ASAN 同套件通过（3.28 秒）。
这证明同事件循环停机路径，不替代阶段超时、跨线程取消和派发失败矩阵。
新增 required/optional 静默交换机重放耗尽 200ms 恢复预算用例，检查原始 timed_out、
帧泵失败、连接关闭、readiness 未恢复及不同错误升级语义。Windows 完整 Application
套件通过（4.86 秒）。随后新增单次 200ms 阶段超时、总预算 1 秒的 required/optional
重试用例：第三次 TCP 会话重放成功，先前错误为 timed_out，交换机参数与原声明逐字节
一致，无预算升级且 readiness 经两次检查恢复。Windows Application 套件通过（5.31 秒）。
派发故障矩阵、完整订阅恢复与真实 broker 验收仍待补齐。
启动分配故障进展：新增预连接 Application start 的前 16 个分配位置注入，
捕获 bad_alloc 或 not_enough_memory 后主动停止监管器并 join，再关闭服务，检查连接
disconnected 且无 shutdown_required。测试要求至少 5 个位置实际注入；未命中位置
不计作已验证故障。Windows 完整分配套件通过（3.80 秒）。此用例显式清理，
不证明生命周期自动回滚或所有异步派发点，后续仍需覆盖注册后执行与低内存收尾。
自动回滚补充：同一 fixture 新增 lifecycle.start 驱动的握手后 16 个位置注入，
在测试主动 stop/join 之前断言启动失败不抛出 bad_alloc、连接 disconnected、
started_services 为空且 shutdown_required 为 false。Windows 完整分配套件通过
（3.83 秒）。要求至少 5 次实际注入，未宣称每个位置均命中；仍不覆盖持续内存耗尽、
全部异步派发点和带完整订阅拓扑的分配失败。
Arch ASAN 新用例实际失败：返回值未满足内存错误断言，started_services 非空，
随后被 CTest 15 秒上限终止。此为当前 T02 优先缺陷，不能用 Windows 通过代替。
已增加注入位置/错误编号/残留登记数量诊断；诊断版本尚待重建运行，根因未定。
诊断版本已重建并定向复现：Arch 在 position=0、opened=2、armed=true 时，
start success=true、error=0、retained=1，随后仍在 15 秒终止；因此并非观察者未启用。
GDB catch throw 和分配函数源码断点未命中预期抛出位置，实际失败路径仍需定位，
不能据此认定为生产回滚缺陷或编译器缺陷。调试进程已停止，未放宽断言/超时。
根因证据：直接断在 __cxa_throw 后，栈为 allocate → epoll async_read → fixture 的
peer 协程；线程局部注入器误伤同线程脚本 broker，非服务注册路径。新增实际抛出计数
确认 position=0 触发一次。现将 broker 放到独立线程/事件循环，故障只注入客户端；
peer 不更新非线程安全的测试断言计数，异常存入任务并在 join 后读取。
自动回滚断言不变，独立恢复和手动清理用例也使用隔离后的 fixture。
隔离修复后 Windows/Arch ASAN 完整分配套件通过（3.81/3.85 秒），先前的该 fixture
超时已解决，不再作为当前生产回滚缺陷。进一步将判定改为分配器实际抛出计数差，
检查每次最多一次，观察者必须已启用；两类 Application 用例均要求 16/16 位置实际
命中，移除逐位置临时日志。Windows 加强版完整套件通过（3.81 秒）。
下一步是会话执行阶段/完整订阅拓扑故障，不能把启动 16 个位置扩称全部分配安全。
会话故障补充：直接等待 async_run_session 的 32 个分配位置扫描，要求至少 16 次
实际抛出；命中则异常向调用者传播，未命中则 ready 一次并主动取消，返回后显式关闭
连接。脚本 broker 使用独立线程，未放宽原有启动/回滚断言；场景选择改为命名枚举，
避免多组布尔参数歧义。Windows 完整分配套件通过（3.81 秒）。此用例为空拓扑，
不证明完整订阅重放阶段及真实 broker 故障，也不证明关闭 OTEL 的 CPU 零回归。
Arch 新会话用例未通过（8 个断言，进程正常结束）：部分注入时 ready=0 且会话直接
返回，另有 ready 后的注入不抛异常。下一步检查返回的协议错误和取消归属，判定现有
断言是否正确表达 API 契约；已加位置/ready/实际注入次数/错误枚举诊断，尚待重建。
确认 position 13/14 的 I/O 失败在 ready 前、16/17 在 ready 后返回 connection_closed。
修复 transport_error 对内存不足的分类：追加 not_enough_memory 枚举，不增加协议错误
结构字段，Application 的连接/会话失败及启动屏障保留该原因。测试接受明确的内存
错误 result 或 bad_alloc，而非任意失败；临时日志已移除。Windows 两套件通过
（5.40/3.81 秒），Arch ASAN 两套件通过（4.15/4.30 秒）。完整订阅及真实 broker 仍待验收。
Application 订阅恢复增量：现有生命周期 fixture 增加服务端命名队列、绑定、消费者，
初始订阅完成才断线；新会话队列名变化，绑定/订阅检查映射，恢复后发送一条消息到
原回调，校验标签、编号、重投标志及正文，仅投递一次。普通恢复和阶段超时重试共用
此链路；Windows Application 套件通过（5.38 秒）。手动 ACK、标志/字段表及真实
broker 仍待覆盖，不将该脚本验收扩称完整消息可靠性。
手动确认审计：delivery 不携带消费通道/会话身份，恢复创建的新 logical_channel
未暴露给原回调。捕获旧 channel 的 ACK 被 generation 检查拒绝；这避免误确认，
但当前没有可用的恢复后手动确认闭环。已在 Application 真实投递后的脚本路径验证
旧 channel ACK 返回 channel_closed，直到对端结束也没有 Basic.Ack 线帧。
下一实现方向：新增显式带确认上下文的消费入口，确认凭据绑定连接、会话代数、通道
和 delivery tag；重放时重绑定，旧凭据失效。保留旧 delivery_handler 的直接调用路径，
不在旧消息结构塞 weak_ptr，不用全局 tag 查询或让旧通道重新有效。
可在订阅/重放阶段通过 handler factory 构造新回调，避免每条旧模式消息增加查表或
上下文分配。须覆盖 ACK/NACK、过期凭据、取消、重放与 broker 线帧后才能关闭该缺项。
确认入口基础已加入 delivery_acknowledgement 独立接口/实现，持有弱连接、会话代数、
通道、投递编号；ack/nack 返回按值保存身份的任务，写锁内复核代数后发送。
当前仅基础实现：尚无消费回调构造入口，尚未处理单通道关闭后的失效，也未做 ACK/NACK
线帧测试；不能作为可用闭环交付。旧 delivery 和回调调用路径没有改动。
已接入 async_consume_acknowledged：模板化共享订阅协议实现，旧模式不构造每消息
确认包装；新模式保存原上下文回调，重放时重新绑定。Windows Application 套件通过
（5.65 秒），新增普通恢复/阶段超时后恢复的 Basic.Ack 通道、tag 和 flags 线帧断言。
单通道关闭、NACK、旧确认对象跨会话及分配故障仍待补齐；恢复记录增加一个函数存储，
不能宣称订阅分配/内存或全框架性能已保持不变。
确认线帧补充：恢复后 Basic.Nack 的 method=120、tag=7、flags=0（不重新入队）
及 flags=3（multiple+requeue）均有脚本断言；后者覆盖阶段超时后恢复及 optional 服务。
ACK/NACK 对象在 lifecycle.stop 后均返回 channel_closed，最终仅观察到一次有效确认。
Windows Application 套件通过（5.98 秒）。单通道关闭、跨新会话的旧确认对象、全部
flags 组合以及真实 broker 重新投递行为仍待验收。

T02 追加回归：旧确认对象跨会话复用 channel=1、delivery_tag=7 时，预先创建但延迟执行的
ACK 任务和旧 NACK 均被拒绝，新对象仍能确认。新增单通道关闭回归先在 Windows 复现：
收到 Channel.Close/发送 CloseOk 后，旧对象仍返回成功，对端共收到三次确认而非一次。
修复在首次关闭挂起前移除通道的确认登记、消费回调和未完成内容；确认操作在写锁内
同时检查会话代数与现有通道登记。发布确认分发不再为未知/已关闭通道创建新登记。
没有增加一份通道容器或逐消息分配，但尚无全框架性能等价证据。
修复后 Windows Release 两组通过（Application 6.25 秒、分配故障 3.78 秒），
Arch ASAN 两组通过（5.14 秒、4.03 秒）；476 个具名模块依赖边界检查通过。
本轮仍未覆盖本地主动 Channel.Close 的失败/分配矩阵、等待写锁时的关闭竞争，以及
旧 logical_channel 在远端单通道关闭后的全部 API 拒绝语义；这些继续归 T02。
真实 broker、全部确认 flags 与订阅注册事务性、性能验收均未结项。

T02 主动关闭追加：脚本对端接收 Channel.Close 后不回复而断开，复现首次失败、二次
调用却成功的问题。logical_channel 的布尔开关改为 uint8_t 的 open/closing/closed；
只收到 CloseOk 才进入 closed 并允许幂等成功，未确认关闭再次调用返回 channel_closed。
进入 closing 在清理/观察者通知之前，防止同步回调重入时仍按 open 发起操作。
新增断线失败与正常 CloseOk 的对照，后者验证重复关闭不再发第二个 Channel.Close。
最终 Windows 两组通过（6.23 / 3.83 秒），Arch ASAN 两组通过（5.19 / 3.98 秒）。
这不代表关闭取消/分配故障或全部重入场景通过；新状态不增加堆分配，但性能等价仍归 T07。
构建仍有旧消费分支 acknowledged_handler 缺显式初始化的 Clang 警告，订阅事务审计时一并处理。

T02 订阅前置准备：新增会抛 bad_alloc 的 callable 复制，旧 consume 与 acknowledged
consume 的 no_wait 路径均先复现失败前已经发出 Basic.Consume（脚本对端两次断言失败）。
回调恢复副本及新模式 wrapper 现于网络发送前准备，复制次数不增加，旧消息回调仍直接调用。
修复后相同回归不出现消费帧，通道仍可按正常/断线两条关闭路径收尾；同时消除旧消费分支
acknowledged_handler 缺初始化警告。Windows 两组通过（6.19 / 3.83 秒），Arch ASAN
两组通过（5.44 / 4.25 秒），476 模块依赖检查通过。
仍须完成 ConsumeOk 后 tag 分配、handler 容器插入及 topology remember 的异常事务；
本轮只消除 callable 准备失败的远端副作用，不能称整个订阅注册已经强异常安全。

T02 订阅注册守卫：收到 ConsumeOk（或 no_wait 发送成功）后，解析/登记异常触发无分配
会话中断和该通道本地登记清理；成功登记后解除守卫，并移动返回 tag，避免提交后的返回复制。
无 reader 且可获得写锁时同步释放传输；有 I/O 所有者时保留借用资源，交给其收尾。
新增畸形 ConsumeOk 的脚本用例，验证 malformed_frame 保留、通道失效、独占连接进入
disconnected。Windows 两组通过（6.16 / 3.86 秒），Arch ASAN 两组通过（5.19 / 3.87 秒）。
这是防止半注册的实现起点，不是强异常安全验收：下一步必须覆盖 RPC 已到远端但尚未
返回调用方的异常窗口、分配注入、活动 reader、只有活动 writer 无 reader 的最终清理、
其他通道确认回调的收尾与恢复记录不被误删。尤其 async_close 的 transport_interrupted
早退尚需核对，不能将会话 interrupted 直接等同于全部资源已释放。

T02 活动 reader 回归：在帧泵已启动时发送畸形 ConsumeOk，订阅原始 malformed_frame
保留且通道失效，但初版 reader 返回成功（Windows 新测试复现）。现在帧循环因
transport_interrupted 退出时返回 connection_closed，显式取消仍优先返回 cancelled；
没有增加运行中每帧处理或分配，只在退出分支区分故障。测试等待 reader 完成后再检查
disconnected，并执行连接关闭，不将状态标志替代协程等待。Windows 两组通过
（6.21 / 3.84 秒），Arch ASAN 两组通过（5.15 / 4.19 秒），476 模块依赖检查通过。
仍缺真实 TLS、并发 writer、分配失败矩阵及跨通道清理；不能据此宣称 T02 完成。

T02 跨通道收尾：给另一通道注册确认观察者后，无 reader 的订阅中断路径未通知其失败，
修复前 Windows 两处断言为 0 而非 1；活动 reader 对照通过。独占传输释放后现统一执行
待处理 RPC 和确认清理，通知完成才发布 disconnected，期间保持 closing 阻止连接重入。
测试检查另一通道失败回调一次、再次 close 不重复通知，独占通知时仍为 closing。
这里未发布待确认消息，仅证明跨通道观察者登记被正确收尾，不替代未确认消息重投验收。
Windows 两组通过（6.17 / 3.83 秒），Arch ASAN 两组通过（5.20 / 3.89 秒），
476 模块依赖检查通过。只有 writer 活动而无 reader 的所有权分支仍是下一项。

T02 活动 writer 回归：另一通道启动 32 MiB 发布并确认任务尚未完成，再返回畸形
ConsumeOk；无独立 reader。修复前 close 提前成功，最终仍 closing，确认失败通知为 0。
interrupted 的关闭路径现等待写锁，释放传输、清理 RPC/确认后进入 disconnected；
共用 release_transport 以免三个收尾位置重复维护 TLS/socket 释放顺序。
新回归验证发布最终失败、连接关闭和其他通道通知一次；Windows Application 6.28 秒、
Arch 两组 5.18 / 3.86 秒通过。此处是脚本 TCP，不等于真实 TLS/broker 或硬性能验收。
下一步补并发 close 的代数复核（等待写锁期间旧关闭不能影响新会话）、注册分配矩阵，
并核对普通发布失败中断路径；仍不宣称 T02 已完成。

T02 并发关闭：活动 writer 用例改为 when_all 同时等待两次 close，两者成功且最终
disconnected、其他通道失败通知仍为一次。interrupted 关闭在获得写锁后复核原会话
代数、已关闭状态及 reader 所有权，避免过期关闭继续释放传输。Windows Application
6.21 秒通过，Arch 两组 5.17 / 3.88 秒通过；依赖检查及 AGENTS 同步检查通过。
当前并发测试未强制新会话插入两个 close 之间，代数变化分支仍缺定向调度证据；
注册分配矩阵、旧逻辑通道在远端 Channel.Close 后的所有 API 失效仍未补齐。

T02 校验优先级回归：将回调复制前移后，no_wait 缺 consumer_tag 被 callable 的
bad_alloc 替代；旧、新入口均先复现失败。现先完成参数/字段表校验，再准备回调，
最后发送，恢复 precondition_failed 的返回并保持复制失败不发消费帧。
Windows 两组通过（6.21 / 3.81 秒），Arch ASAN 两组通过（5.20 / 3.90 秒）。
该回归不替代注册后分配故障矩阵；T02 仍未验收。

T02 消费回调重入：非空内容原来在 callback 之后才删除接收登记，callback 内立即
关闭通道会先删除该节点，留下消息引用/迭代器失效风险。现用 map.extract 转移原节点
所有权，再调用 handler；无正文复制、无整条消息搬移、无新增节点分配。
新增初次投递 callback 内启动 close，随后仍读取 delivery 元数据和正文的回归；close
随脚本断线失败被等待，随后 Application 完成重连、重放和新消息确认。
最终 Windows Application 6.27 秒、Arch Application ASAN 5.29 秒通过。
这是代码审查定位并修复的风险，没有声称运行过修复前 ASAN 崩溃复现；
旧/新消费入口完整性能对比和剩余异常矩阵仍待验收。

T02 发布确认所有权：确认分发从通道容器取出 tracker 的局部 shared_ptr 后再调用
settle，避免同步观察者移除通道登记后 tracker 被提前析构；未知通道仍不创建登记。
活动 writer 的 32 MiB 发布现在先启用 publisher confirms，覆盖待确认发布失败后的
关闭通知，而不只是空 tracker 上的观察者登记。Windows Application 6.34 秒、Arch
Application ASAN 5.24 秒通过。观察者销毁最后一个 tracker 所有者的定向重入用例
尚未单独加入，不将一般套件通过当作该精确分支已验证。

T02 订阅分配矩阵：隔离对端在线程独立 io_context 运行，客户端逐次注入 no_wait
acknowledged consume 分配故障。前 32 个位置全触发故障，未到达成功路径；扩大到
128 个位置，并断言实际失败数量至少 8 且小于 128，确保同时覆盖失败和完整成功。
每个失败要求 bad_alloc 或明确 not_enough_memory，close 后 disconnected，释放调用方
仍持有的回调后弱引用过期。初版两处弱引用断言源于参数尚未移交时测试自己仍持有 callback，
已纠正所有权边界，并非把客户端残留引用豁免。Windows 两组通过（6.32 / 3.89 秒），
Arch ASAN 两组通过（5.42 / 3.98 秒）。仍缺等待 ConsumeOk、活动 reader、旧消费入口
的同等注入矩阵及真实 broker 验证；不把 no_wait 子集标为整个注册事务完成。

T02 ConsumeOk 分配矩阵：在同一线程隔离对端内解析客户端 Basic.Consume 并返回带完整
consumer tag 的 ConsumeOk，覆盖 broker 已确认、随后本地 handler/topology 注册失败的
窗口。其余测试结构沿用 no_wait 128 位矩阵：失败保留 bad_alloc/not_enough_memory 语义，
连接关闭、调用方回调引用释放，并要求同时覆盖至少一个成功位置。Windows 定向和完整
两组通过（Application 6.23 秒、overhead 3.96 秒），Arch ASAN 两组通过（5.27 / 4.05 秒），
476 模块依赖检查通过。还缺带活动 reader 的该矩阵、旧 delivery_handler 等价矩阵，以及
真实 RabbitMQ 的 broker 端取消/重投行为；T02 未完成。

T01 源码迁移和选定构建回归完成，下一步集中推进 T02；T03 使用现有故障代理草稿继续，
不另起一套实现。发现其他协议缺项时登记 T04，不再插队零散开发。

gRPC 自动观测补充：Application 的 grpc_client_service 现在只向用户暴露
instrumented_grpc_client；该 facade 在 trace/metric sink 均为空时以内联 lvalue/rvalue
精确转发直接返回原 grpc::client task。最初的跨模块按值转发在 MSVC 上额外构造一个
multimap 哨兵（每次 96B），由新的禁用分配回归捕获后已移除，不能将其视作可接受开销。
启用时覆盖 unary、client/server/bidi streaming，生成 rpc.client.duration 指标并将
本地导出的 client span 身份直接注入 gRPC metadata，避免出现额外未导出的传播子 span。
Windows Release 与 Arch ASAN 的 Application + disabled-overhead 套件均通过；这只完成
gRPC client 的局部 T04/T07 证据，未替代 gRPC server、真实服务和其余协议的验收。

gRPC server 补充：Application 的 grpc_server_service 现将 router handler 包装为
RPC 语义适配器。启用 telemetry 时它导出 `rpc.server.duration`，创建 server span，
并记录 service、method 与 `grpc-status`；当 HTTP tracing middleware 已建立上下文时
它作为其子 span，未安装该 middleware 时直接解析请求的 W3C `traceparent`/`tracestate`。
trace 和 metric sink 都为空时原 handler 原样返回，不新增 middleware coroutine 或解析工作。
Arch clang++ 22 最小 HTTP+gRPC ASAN 构建首次揭露两个隐式传递 import，已改为显式导入
HTTP handler 与 task 模块；随后 `test_grpc`、`test_application_integrations` 通过（0.25s）。
Windows Release 的 gRPC、Application、disabled-overhead 三组通过（10.22s），HTTP+Redis
且 gRPC 关闭及协议全关的 core 均重建通过。此项仍未满足真实 gRPC server、跨进程传播、
错误/取消状态全矩阵及性能基准，故 T04/T07 仍保持未验收。

gRPC server 直接适配器回归：新的 request-context 用例覆盖未安装 HTTP tracing middleware
时的入站 W3C parent。它断言 server span 继承远端 trace ID 与 parent span ID、角色为
server，`UNAVAILABLE` 映射为 operation error，且导出唯一 `rpc.server.duration` 并携带
`rpc.grpc.status_code=14`。首次 Windows 验证错误复用了旧 core 静态库（测试构建禁用了
project-reference build），现已把核心库显式重建作为源码变更后的验证前置；随后 Windows
Release 与 Arch clang++ 22 ASAN 的 `test_grpc` 均通过。Arch 同时要求测试直接导入
io_context/socket，已修正传递 import。此为进程内 adapter 行为证据，仍不等价于跨进程
gRPC server 互操作、取消与异常的完整矩阵。

gRPC 旁路故障隔离：server adapter 的 span exporter 与 metric sink 均注入抛异常的
实现，业务 handler 仍完成且返回 `grpc-status=0`。Windows Release 与 Arch clang++ 22
ASAN 的 `test_grpc` 均通过。这证明 exporter 故障不会改变该 gRPC handler 的协议结果；
不替代 collector 队列满、网络断开或跨进程服务端的验收。

OTLP 可靠投递复核：Windows Release 与 Arch clang++ 22 ASAN 的
`test_otlp_exporter` 均通过（1.35s / 0.66s）。该套件实际覆盖 trace/metric/log 三信号
独立开关、禁用时无 span sink/队列、lazy producer 不执行、记录校验不消耗队列、容量
上限与丢弃统计、采样、HTTP 部分成功/重试、真实本地接收器 wire 解析、flush/shutdown
以及永久 collector 故障不搁置 flush。它是 T05 的可靠投递证据；各协议的关闭 CPU/延迟
基准、日志关联、全组件生产者覆盖和 collector 长期不可达压力仍未验收。

gRPC metric 生命周期与关闭回归：在 HTTP+gRPC Arch ASAN 的
`test_http_disabled_overhead` 中，启用 client span/metric 传播用例发现
`grpc_duration_scope` 将 request 的 service/method 保存为 `string_view`，请求被 move
给原 client 后终态指标发生 heap-use-after-free。scope 现在只在 metric sink 已安装时
拥有并复制两个字段；禁用时不复制、不取时钟，继续直接转发原 RPC。ASAN 57 项套件通过
（3.41s），Windows 显式重建 core 后关闭开销与 gRPC 套件通过（3.93s / 0.01s）。同次
clang 构建还揭露 allocation regression 对 recovery_policy 的传递 import，已改为直接
导入；Windows 测试的 MongoDB include 顺序恢复为先声明 helper 再包含 ping case。该证据
证明当前已覆盖入口的内存安全和关闭分配基线，没有把局部测试扩展为全协议性能结论。

gRPC 四形态终态回归：`test_grpc` 现用超过 SSO 长度的 service/method，分别运行 unary、
client-streaming、server-streaming、bidi-streaming；每次请求在传入原 client 后移动，随后
断言 client span 的 parent/trace identity 及 `rpc.client.duration` 的 service/method 属性。
这直接覆盖此次 owned metric metadata 修复的所有公开调用形态。Windows Release 和 Arch
clang++ 22 ASAN 均通过（0.01s / 0.07s）。取消/deadline 的四形态远端服务验证仍属于 T04。

gRPC 取消旁路回归：启用 span/metric 后向 unary 传入预取消 token，原 gRPC terminal status
保持 `cancelled`，span operation status 也是 cancelled，metric 的
`rpc.grpc.status_code` 为 1。Windows Release 与 Arch clang++ 22 ASAN 均通过。此处覆盖
预取消；中途取消、deadline、断流及 streaming 的远端服务场景仍未验收。
