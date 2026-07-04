# Raft 生产化设计

本文档描述 cnetmod Raft 的目标边界和当前实现组织。目标不是做 demo，而是把 Raft 拆成可测试、可替换、可运行的协议栈。

## 模块边界

- `types.cppm`: Raft 稳定类型，包含 term/index、日志条目、配置、snapshot、RPC 请求响应。
- `configuration.cppm`: quorum、joint consensus、投票统计。
- `storage.cppm`: 持久化接口，分 hard state、snapshot metadata、log。
- `memory_store.cppm`: 测试和嵌入式场景使用的内存存储。
- `leveldb_store.cppm`: LevelDB 持久化适配，使用二进制长度前缀编码，支持 hard state、snapshot metadata、日志截断。
- `log_manager.cppm/.cpp`: 日志一致性、冲突截断、leader append、snapshot restore。
- `progress.cppm`: follower 复制进度，支持 `probe/replicate/snapshot` 状态和 inflight 窗口。
- `fsm.cppm`: 已提交日志顺序 apply 的状态机入口。
- `node.cppm/.cpp`: Raft 状态机，负责 term、投票、leader/follower 转换、commit 推进、配置变更和 snapshot 安装。
- `wire.cppm`: Raft RPC 二进制帧编码，格式为 magic/version/length/body。
- `transport.cppm`: 传输抽象接口。
- `tcp_transport.cppm`: 基于 cnetmod `async_accept/async_connect/async_read/async_write` 的 TCP 发包和收包适配。
- `runtime.cppm`: Raft 节点运行时，负责 TCP 服务循环、选举定时器、heartbeat/replication 定时器和停止取消。

## 网络路径

发送路径：

1. `raft_node` 生成 `request_vote_request`、`append_entries_request` 或 `install_snapshot_request`。
2. `raft_tcp_transport` 将请求封装成 `raft_rpc_message`。
3. `wire.cppm` 编码为二进制 frame。
4. `tcp_transport.cppm` 通过 `async_connect` 连接 peer，通过 `async_write` 写完整 frame。

接收路径：

1. `raft_tcp_transport::serve()` 使用 `tcp::acceptor` 和 `async_accept` 接收连接。
2. 连接处理协程用 `async_read` 读取 9 字节 frame header，再读取完整 payload。
3. `wire.cppm` 解码为 `raft_rpc_message`。
4. transport 根据消息类型调用 `raft_node::handle_request_vote`、`handle_append_entries`、`handle_install_snapshot` 等。
5. 响应再通过 transport 发回来源节点。

## 已实现能力

- RequestVote / PreVote 基础流程。
- AppendEntries 日志匹配、冲突截断、commit index 推进。
- Leader 上任写入当前 term no-op，避免提交旧 term 日志。
- 单节点和多节点 majority commit。
- Joint consensus 配置变更基础判断。
- Snapshot metadata 安装和日志 reset。
- FSM 顺序 apply committed entries。
- LevelDB 持久化 hard state、snapshot metadata、log entry。
- TCP transport 的实际发包、收包、dispatch、响应路径。
- Runtime 驱动的自动选举、heartbeat 和复制 tick。
- 可取消的 TCP accept loop 和 timer loop。
- Snapshot 文件 payload 通过 Raft TCP frame 发送；接收端落地到本地 snapshot 目录后安装 metadata。
- ReadIndex 通过 heartbeat context 和 follower ack 做 quorum 确认。
- Leader lease 只在当前任期获得多数派活动后有效，`check_quorum` 到期会 step down。
- TCP transport 按 peer 复用长连接，发送失败后关闭连接并按退避策略重连。
- LevelDB hard state 和 log 的重启恢复测试已覆盖。

## 仍需完成的生产项

- Snapshot reader/writer 的流式大文件分片和校验和。
- 持久化批量写、可配置 fsync 策略、磁盘错误注入。
- Leader lease 的跨机器时钟漂移配置和观测指标。
- Follower lag 过大时自动切换 install snapshot。
- Membership change 的 apply 后生效、leader 自移除处理和配置变更串行化。
- 连接池限流、metrics 和 backpressure。
- 跨平台故障测试：断网、乱序、重复包、磁盘错误、snapshot 后恢复。
