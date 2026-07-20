# Raft

cnetmod 提供一套 Raft 复制状态机协议栈，用于构建强一致服务。它不是只给
demo 用的 helper：选举、日志复制、存储、snapshot 传输、成员变更、线性读、
TCP transport、TLS / 认证钩子、指标和运行时循环都拆成了明确接口。

适合使用 Raft 的场景包括 metadata 服务、配置中心、primary-backup 控制面、
小型分布式 KV、分片存储的元数据复制等。生产环境不要把大对象 payload 直接
写进 Raft log；大文件应放在 Raft 外部存储，Raft 只保护 metadata、版本、归属
和删除顺序。

## 模块组织

常规使用导入聚合模块：

```cpp
import cnetmod.protocol.raft;
```

内部实现按 partition 拆分：

| Partition | 职责 |
|-----------|------|
| `types` | term、log index、角色、日志条目、hard state、RPC 结构、错误、指标和选项。 |
| `configuration` | voter / learner 集合、quorum、joint consensus、多数派判断。 |
| `storage` | hard state、snapshot metadata、log entry 的持久化接口。 |
| `memory_store` | 内存存储，用于测试、示例和明确不要求持久化的嵌入场景。 |
| `leveldb_store` | LevelDB 持久化存储，支持批量日志写入、重启恢复和可选同步写。 |
| `log_manager` | 日志匹配、append、冲突截断、commit 推进、compaction 和 snapshot restore。 |
| `progress` | follower 复制进度，包含 `probe`、`replicate`、`snapshot` 和 inflight 背压。 |
| `fsm` | 用户状态机 apply，以及 snapshot save/load 回调。 |
| `node` | Raft 状态机：选举、leader 转换、复制决策、ReadIndex、成员变更、snapshot、leader transfer。 |
| `wire` | Raft RPC 二进制 frame 编解码，以及损坏/截断校验。 |
| `transport` | `raft_node` 使用的抽象 transport 接口。 |
| `tcp_transport` | 真实 TCP transport、长连接复用、认证/TLS 钩子、snapshot chunk 传输、重试、队列和指标。 |
| `runtime` | TCP 服务、随机选举 tick、heartbeat/replication tick、自动 snapshot、清理和取消。 |

## 公共模型

核心公开类型：

| 类型 | 作用 |
|------|------|
| `raft_config` | 本节点 id、peer id 列表和 `raft_options`。 |
| `raft_options` | 选举超时、heartbeat、leader lease、ReadIndex 超时、pre-vote、check-quorum、append 批量、inflight 窗口、snapshot chunk size。 |
| `raft_node` | 确定性的 Raft 核心，可用于进程内单测，也可由 transport/runtime 驱动。 |
| `raft_node_runtime` | 运行 TCP server、选举循环、heartbeat/复制循环和可选自动 snapshot。 |
| `raft_tcp_transport` | TCP 收发 Raft RPC，维护 peer 地址、连接复用、重试/退避、安全配置和指标。 |
| `raft_storage` | 存储接口，可自行实现，也可使用 `memory_store` / `leveldb_store`。 |
| `state_machine` | 通过 `on_apply`、`save_snapshot`、`load_snapshot` 应用已提交命令并生成/加载 snapshot。 |
| `raft_metrics` | 节点角色、term、log index、applied index、snapshot index、voter/learner 数量、pending read、活跃 follower、fatal error。 |
| `raft_peer_transport_metrics` | 每个 peer 的排队发送、成功、失败、重连、最后发送/接收时间、延迟和最后错误。 |

## 基础拓扑

每个 Raft 节点需要稳定 node id、peer 列表、持久化存储、状态机、TCP transport
和监听地址。

```cpp
import cnetmod.core.address;
import cnetmod.io.io_context;
import cnetmod.protocol.raft;

namespace raft = cnetmod::raft;

class metadata_fsm final : public raft::state_machine {
public:
    void on_apply(const raft::log_entry& entry) override {
        if (entry.type != raft::entry_type::command)
            return;
        // 解码 entry.command，并修改本地业务状态。
    }
};

auto store = std::make_shared<raft::leveldb_store>("data/raft-n1");
metadata_fsm fsm;

raft::raft_options opts{
    .election_timeout = std::chrono::milliseconds{300},
    .heartbeat_interval = std::chrono::milliseconds{75},
    .leader_lease_timeout = std::chrono::milliseconds{750},
    .read_index_timeout = std::chrono::milliseconds{1000},
    .pre_vote = true,
    .check_quorum = true,
    .lease_read = false,
    .max_entries_per_append = 256,
    .max_inflight_append = 512,
};

raft::raft_node node{
    raft::raft_config{.id = "n1", .peers = {"n2", "n3"}, .options = opts},
    store,
    &fsm,
};
```

真实网络节点要配置 peer endpoint 并启动 runtime：

```cpp
raft::raft_tcp_transport transport{*ctx, "n1", raft::raft_tcp_transport_options{
    .max_send_attempts = 3,
    .retry_backoff = std::chrono::milliseconds{20},
    .snapshot_directory = "data/raft-n1/snapshots",
    .snapshot_chunk_size = 1024 * 1024,
    .max_outbound_queue = 4096,
}};

transport.add_peer(raft::raft_tcp_peer{.id = "n2", .address = n2_endpoint});
transport.add_peer(raft::raft_tcp_peer{.id = "n3", .address = n3_endpoint});

raft::raft_node_runtime runtime{
    *ctx,
    node,
    transport,
    n1_listen_endpoint,
    opts,
    raft::raft_runtime_options{
        .start_tcp_server = true,
        .auto_election = true,
        .auto_heartbeat = true,
        .auto_snapshot = true,
        .snapshot_policy = raft::raft_snapshot_policy{
            .log_entries_threshold = 100000,
            .min_interval = std::chrono::minutes{5},
            .uri_prefix = "data/raft-n1/snapshots/n1",
        },
    },
};

runtime.start();
ctx->run();
```

完整可运行的 loopback TCP 示例：

- `examples/raft/redis_cluster.cpp`
- `examples/raft/oss_shared_storage.cpp`
- `examples/raft/raft_demo_cluster.hpp`

## 写路径

正常写入流程：

1. 业务服务在当前 leader 上接收写命令。
2. leader 调用 `raft_node::append_command(command)`。
3. `raft_tcp_transport::replicate_to_all(node)` 向 follower 发送 AppendEntries
   或 InstallSnapshot。
4. follower 校验 term、prev log index/term，追加日志，更新 commit index 并响应。
5. leader 收到多数派确认后推进 commit。
6. `state_machine::on_apply(entry)` 按 log 顺序被调用。

如果本节点不是 leader，`append_command` 返回 `raft_errc::not_leader`。生产服务
应把写请求路由到已知 leader，或向客户端返回 redirect/retry 信号。当前 leader
可以通过 `raft_node::leader_id()` 和 `raft_metrics::leader_id` 观察。

## 线性读

cnetmod 支持 ReadIndex 风格线性读：

```cpp
auto response = co_await runtime.async_read_index(raft::read_index_request{
    .id = next_request_id(),
    .context = "metadata-read",
});

if (!response)
    co_return response.error();

// 确认本地状态机 applied index >= response->index 后，可以从本地状态机读。
```

行为边界：

- 单节点 leader 可以立即返回 ready ReadIndex。
- 多节点 leader 会把 read context 放进 heartbeat / AppendEntries，等待多数派 ack。
- pending read 按 `raft_options::read_index_timeout` 过期。
- 节点 step down 时会清理 pending ReadIndex。
- lease read 要求 `check_quorum = true`；`lease_read = true` 且
  `check_quorum = false` 会被拒绝，避免不安全读。

如果要在 follower 上读，必须同时满足 ReadIndex 返回和本地 apply 进度要求。不要
在没有业务级 stale-read 契约的情况下直接读 follower 旧状态。

## 选举、Lease 和 Check-Quorum

默认策略偏保守：

- `pre_vote = true` 避免隔离节点无意义地抬高 term。
- `check_quorum = true` 让 leader 在 leader lease timeout 内失去多数派通信后主动 step down。
- `leader_lease_timeout` 控制 `leader_lease_valid()` 和 `check_leader_quorum()` 使用的多数派活跃窗口。

推荐时序关系：

```text
heartbeat_interval < leader_lease_timeout < election_timeout
```

网络抖动明显时，应适当增大 election timeout，同时保持 heartbeat 足够短，用来及时发现多数派丢失，但不能短到压垮慢 peer。

## 成员变更

API 包括：

- `set_learners(learners)`
- `promote_learner(id)`
- `enter_joint_configuration(new_voters)`
- `leave_joint_configuration()`
- `remove_node(id)`
- `transfer_leader(target)`

运维规则：

- 新节点先作为 learner 加入。
- 等 learner 追平日志。
- 通过 joint consensus promote 为 voter。
- 新配置提交后，再移除旧 voter。
- 如果 leader 移除自己，它会在 leave-joint 提交后 step down。
- leader transfer 目标已追平时立即发送 `TimeoutNow`；未追平时先触发复制，追平后再发送。

learner 会复制日志，但不参与 quorum，也不能发起有效选举。

## Snapshot 和 Compaction

Snapshot 分两层：

- `snapshot_metadata`: last included index/term、URI 和 configuration。
- snapshot payload: 由业务 `state_machine` 生成/加载的实际状态。

手动 snapshot：

```cpp
auto snapshot = node.create_snapshot("data/raft-n1/snapshots/n1-100.snapshot");
if (!snapshot)
    handle(snapshot.error());
```

自动 snapshot：

```cpp
raft::raft_runtime_options{
    .auto_snapshot = true,
    .snapshot_policy = raft::raft_snapshot_policy{
        .log_entries_threshold = 100000,
        .min_interval = std::chrono::minutes{5},
        .uri_prefix = "data/raft-n1/snapshots/n1",
    },
};
```

Transport 行为：

- `install_snapshot_request::uri` 非空且 `data` 为空时，TCP transport 会按 chunk 发送文件。
- 内存 snapshot data 大于 chunk size 时，也会切分发送。
- 每个 chunk 有 CRC32，完整文件也有 CRC32。
- follower 先写临时文件，校验 offset 和 checksum，再移动到 snapshot directory。
- receiver 返回 accepted offset 后，sender 可以从该 offset 继续发送。

保留策略：

```cpp
raft::raft_tcp_transport_options{
    .snapshot_directory = "data/raft-n1/snapshots",
    .snapshot_retention = raft::raft_snapshot_retention_options{
        .keep_last = 3,
        .min_age = std::chrono::minutes{10},
    },
};
```

`raft_node_runtime` 在 leader heartbeat loop 中调用 `cleanup_snapshot_files()`。
清理逻辑枚举 transport snapshot directory 下的 `.snapshot` 文件，在超过
`keep_last` 且达到 `min_age` 后删除旧文件。

## 存储

`memory_store` 只适合测试、示例和明确不需要持久化的场景。进程重启恢复应使用
`leveldb_store`：

```cpp
auto store = std::make_shared<raft::leveldb_store>("data/raft-n1/leveldb");
store->set_sync(true); // 如果你的持久化策略要求同步写
```

存储接口负责持久化：

- current term、vote、commit index、last applied index；
- snapshot metadata；
- log entries；
- prefix/suffix truncate；
- reset-to-snapshot 状态。

如果关键存储操作抛异常，`raft_node` 会进入 stopped 状态，并通过
`fatal_error()` 和 metrics 暴露 fatal error。

## TCP Transport

`raft_tcp_transport` 是真实网络路径：

- `serve(endpoint, cancel_token)` 接受 peer 连接；
- 编解码二进制 Raft frame；
- 分发请求到 `raft_node`；
- 通过同一 transport 路径写回响应；
- 复用 peer 长连接；
- 按 `max_send_attempts` 和 `retry_backoff` 重试；
- 按 `max_outbound_queue` 做背压；
- follower 落后于 compaction 后自动切换 snapshot install；
- 记录每个 peer 的健康和延迟指标。

观察 transport：

```cpp
for (const auto& m : transport.peer_metrics()) {
    std::println("{} queued={} ok={} fail={} reconnects={} last_error={}",
                 m.peer,
                 m.queued_sends,
                 m.send_successes,
                 m.send_failures,
                 m.reconnects,
                 m.last_error.message());
}
```

## 认证和 TLS

Transport 安全配置在 `raft_tcp_security_options`：

```cpp
raft::raft_tcp_transport_options{
    .security = raft::raft_tcp_security_options{
        .shared_secret = "...",
        .require_auth_token = true,
        .enable_tls = true,
        .require_peer_certificate = true,
        .peer_certificate_sha256 = {
            {"n2", "expected-sha256-fingerprint"},
            {"n3", "expected-sha256-fingerprint"},
        },
        .client_tls = &client_ctx,
        .server_tls = &server_ctx,
    },
};
```

说明：

- `shared_secret` 用于保护 Raft frame 的认证 token。
- 启用 OpenSSL 后，TLS 需要配置 `ssl_context`。
- mTLS 场景可以按 node id pin peer certificate SHA-256 指纹。
- 生产环境应把 node id、peer address、证书和 shared secret 放在受控集群配置源里。

## 错误处理

大多数写操作返回 `std::expected<T, raft_error>`。常见错误码：

| Code | 含义 |
|------|------|
| `stale_term` | 请求/响应 term 已过期。 |
| `log_inconsistent` | 日志匹配失败，或 committed entry 缺失。 |
| `not_leader` | 本节点不是 leader，不能执行 leader-only 操作。 |
| `not_voter` | 操作要求目标是 voter。 |
| `configuration_error` | 成员变更非法或存在并发成员变更。 |
| `snapshot_required` | follower 需要 snapshot，不能继续普通 append。 |
| `storage_error` | 持久化存储或 snapshot 文件操作失败。 |
| `state_machine_error` | 用户状态机 apply/snapshot 回调失败。 |
| `backpressure` | transport 队列或 inflight 窗口已满。 |
| `stopped` | 节点/runtime 已停止，或请求超时。 |

建议安装 transport error handler：

```cpp
transport.set_error_handler([](raft::node_id peer, std::error_code ec) {
    std::println(stderr, "raft peer {} error: {}", peer, ec.message());
});
```

## 分布式存储示例

仓库包含两个基于 TCP 的业务示例。

`examples/raft/redis_cluster.cpp`：

- 启动三节点 loopback TCP Raft 集群，端口 `19101..19103`；
- 暴露 Redis 风格业务方法：`SET`、`DEL`、`INCR`；
- 业务命令通过 Raft commit，所有副本 apply 同一条命令流；
- 状态机里演示 TTL 处理。

`examples/raft/oss_shared_storage.cpp`：

- 演示 OSS 风格 metadata 服务；
- primary-backup 副本集使用 `19101..19103`；
- 双分片命名空间使用两个独立三节点 Raft group，端口分别是
  `19201..19203` 和 `19301..19303`；
- 复制 object metadata、etag/version 更新和 delete 顺序。

构建目标在 `examples/CMakeLists.txt`：

```bash
cmake --build <build-dir> --target example_redis_cluster --config Release
cmake --build <build-dir> --target example_oss_shared_storage --config Release
```

## 测试覆盖

主要覆盖在 `testing/tests/test_raft.cpp`，包括：

- 单节点和多数派选举/commit；
- stale vote 和 PreVote；
- AppendEntries 匹配、冲突和重试；
- 二进制 wire codec round trip 和损坏 frame 拒绝；
- snapshot install、snapshot metadata、chunk metadata、resume metadata，以及 compaction 后 follower 追赶；
- 状态机 snapshot save/load；
- joint consensus、并发成员变更拒绝、learner 复制、learner promote、remove node、leader self-removal；
- leader lease、check-quorum、lease-read 约束和 ReadIndex timeout；
- leader transfer 和 TimeoutNow；
- storage failure、snapshot failure 后 stop；
- transport 背压、inflight window、retry 和 peer metrics；
- snapshot retention 清理；
- auth token 拒绝、TLS 配置缺失、mTLS 证书 pinning；
- 真实三节点 TCP loopback 复制；
- 五节点 partition/heal 和 seeded chaos/fuzz 收敛；
- LevelDB hard state 和 log 重启恢复。

运行：

```bash
cmake --build <build-dir> --target test_raft --config Release
ctest --test-dir <build-dir> -C Release -R test_raft
```

## 性能

仓库包含一个进程内确定性 Raft core benchmark：

```bash
cmake --build <build-dir> --target bench_raft --config Release
<build-dir>/testing/bench/bench_raft
```

使用 Windows Visual Studio generator 时，exe 通常在
`<build-dir>/testing/bench/Release/bench_raft.exe`。

Intel Core i9-14900K 上的 Release 结果：

| 场景 | 吞吐 |
|------|------|
| 单节点 append + commit | ~5.17M - 5.50M ops/s |
| 5 节点多数派复制 | ~0.80M - 0.83M ops/s |

这些是 core benchmark 数字，不是 WAN、磁盘 fsync、TLS 全链路端到端吞吐。
真实服务性能会受编译器、allocator、CPU 频率策略、存储同步策略、命令大小、
状态机工作量、网络延迟、snapshot 策略和 TLS/auth 配置影响。

## 生产检查清单

部署 Raft 服务前至少确认：

- node id 稳定，持久化目录稳定。
- 使用 `leveldb_store` 或自定义可靠 `raft_storage`；持久化集群不要用 `memory_store`。
- Raft log 放在可靠存储上，并按持久化/延迟预算选择 `leveldb_store::set_sync`。
- 所有节点的 `heartbeat_interval`、`leader_lease_timeout`、`election_timeout` 策略一致。
- 生产集群启用 `pre_vote` 和 `check_quorum`。
- 配置 snapshot policy、snapshot directory、chunk size 和 retention。
- 实现状态机 `save_snapshot` 和 `load_snapshot`，保证快速恢复。
- 跨信任边界部署时启用 auth token 和 TLS/mTLS。
- 导出 `raft_metrics` 和 `raft_peer_transport_metrics`。
- 覆盖重启恢复、磁盘满/存储错误、leader transfer、成员变更、网络分区、snapshot install、滚动重启测试。
- 把业务 command 编码当作稳定 wire/storage 格式管理，不兼容变更前先做版本化。

## 当前边界

Raft 栈已经覆盖真实服务集成所需的大部分协议能力，但它仍是库层。业务系统仍然需要负责：

- client protocol 和 leader redirect 行为；
- 业务 command 的持久化编码和版本兼容；
- snapshot payload 格式和兼容；
- 集群配置分发；
- metrics 导出格式；
- 证书轮换、备份、恢复、滚动升级等运维自动化。
