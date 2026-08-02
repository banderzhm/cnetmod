# Raft

> Raft 共识算法实现，支持 Leader 选举、日志复制、快照、动态成员变更和 TCP 传输。

**import**: `import cnetmod.protocol.raft;`
**CMake**: `-DCNETMOD_ENABLE_RAFT=ON`
**源码**: `src/protocol/raft/`

## 场景导航

- 我要创建 Raft 集群节点 → [看这里](#场景创建-raft-集群)
- 我要实现自定义状态机 → [看这里](#场景自定义状态机)
- 我要持久化存储 → [看这里](#场景持久化存储)
- 我要配置 TCP 传输层 → [看这里](#场景tcp-传输层)
- 我要理解选举与日志复制 → [看这里](#场景选举与日志复制)
- 我要使用快照压缩日志 → [看这里](#场景快照机制)

## API 参考

### Raft 类型

**签名**: `export enum class node_role { follower, pre_candidate, candidate, leader };`

**签名**: `export enum class entry_type { no_op, command, configuration };`

```cpp
export using term_t = std::uint64_t;
export using log_index = std::uint64_t;
export using node_id = std::string;
export using group_id = std::string;
```

**签名**: `export struct raft_config`

```cpp
struct raft_config {
    node_id id;
    std::vector<node_id> peers;
    raft_options options;

    auto initial_configuration() const -> configuration_state;
    auto cluster_size() const noexcept -> std::size_t;
    auto majority() const noexcept -> std::size_t;
};
```

**签名**: `export struct raft_options`

```cpp
struct raft_options {
    std::chrono::milliseconds election_timeout{150};
    std::chrono::milliseconds heartbeat_interval{50};
    std::chrono::milliseconds leader_lease_timeout{100};
    bool pre_vote = true;
    bool check_quorum = true;
    bool lease_read = false;
    std::size_t max_entries_per_append = 128;
    std::size_t snapshot_chunk_size = 1024 * 1024;
};
```

**签名**: `export struct raft_error { raft_errc code; std::string message; };`

`raft_errc` 枚举值：`ok`, `stale_term`, `log_inconsistent`, `not_leader`, `not_voter`, `configuration_error`, `snapshot_required`, `storage_error`, `state_machine_error`, `backpressure`, `stopped`

### `raft_node` — Raft 节点核心

**签名**: `export class raft_node`

| 方法 | 签名 | 说明 |
|------|------|------|
| 构造 | `raft_node(raft_config, std::shared_ptr<raft_storage>, state_machine* = nullptr)` | |
| `id` | `auto id() const noexcept -> std::string_view` | 节点 ID |
| `role` | `auto role() const noexcept -> node_role` | 当前角色 |
| `current_term` | `auto current_term() const noexcept -> term_t` | 当前任期 |
| `leader_id` | `auto leader_id() const noexcept -> std::string_view` | Leader ID |
| `metrics` | `auto metrics() const -> raft_metrics` | 获取指标 |
| `append_command` | `auto append_command(std::string command) -> std::expected<log_entry, raft_error>` | 追加命令（仅 Leader） |
| `begin_pre_vote` | `auto begin_pre_vote() -> request_vote_request` | 发起 Pre-Vote |
| `begin_election` | `auto begin_election() -> request_vote_request` | 发起选举 |
| `handle_request_vote` | `auto handle_request_vote(const request_vote_request&) -> request_vote_response` | 处理投票请求 |
| `handle_vote_response` | `auto handle_vote_response(const node_id&, const request_vote_response&) -> bool` | 处理投票响应 |
| `handle_append_entries` | `auto handle_append_entries(const append_entries_request&) -> append_entries_response` | 处理日志追加 |
| `handle_append_entries_response` | `auto handle_append_entries_response(const node_id&, const append_entries_response&) -> bool` | 处理追加响应 |
| `handle_install_snapshot` | `auto handle_install_snapshot(const install_snapshot_request&) -> install_snapshot_response` | 处理快照安装 |
| `create_snapshot` | `auto create_snapshot(std::string uri) -> std::expected<snapshot_metadata, raft_error>` | 创建快照 |
| `maybe_create_snapshot` | `auto maybe_create_snapshot(const raft_snapshot_policy&) -> std::expected<std::optional<snapshot_metadata>, raft_error>` | 按策略创建快照 |
| `transfer_leader` | `auto transfer_leader(const node_id& target) -> std::expected<std::optional<timeout_now_request>, raft_error>` | 转移 Leader |
| `enter_joint_configuration` | `auto enter_joint_configuration(std::vector<node_id>) -> std::expected<log_entry, raft_error>` | 联合配置变更 |
| `leave_joint_configuration` | `auto leave_joint_configuration() -> std::expected<log_entry, raft_error>` | 离开联合配置 |
| `set_learners` | `auto set_learners(std::vector<node_id>) -> std::expected<log_entry, raft_error>` | 设置 Learner |
| `promote_learner` | `auto promote_learner(const node_id&) -> std::expected<log_entry, raft_error>` | 提升 Learner |
| `remove_node` | `auto remove_node(const node_id&) -> std::expected<log_entry, raft_error>` | 移除节点 |
| `stop` | `void stop(raft_error error)` | 停止节点 |

### `raft_storage` — 存储接口

**签名**: `export class raft_storage`（抽象接口）

```cpp
virtual auto load_hard_state() -> hard_state = 0;
virtual void save_hard_state(const hard_state&) = 0;
virtual auto load_snapshot_metadata() -> snapshot_metadata = 0;
virtual void save_snapshot_metadata(const snapshot_metadata&) = 0;
virtual auto first_log_index() const -> log_index = 0;
virtual auto last_log_index() const -> log_index = 0;
virtual auto term_at(log_index) const -> term_t = 0;
virtual auto entry_at(log_index) const -> std::optional<log_entry> = 0;
virtual auto entries(log_index first, std::size_t max_entries) const -> std::vector<log_entry> = 0;
virtual void append(const std::vector<log_entry>&) = 0;
virtual void truncate_prefix(log_index first_kept) = 0;
virtual void truncate_suffix(log_index first_removed) = 0;
virtual void reset_to_snapshot(const snapshot_metadata&) = 0;
```

### `memory_store` — 内存存储

**签名**: `export class memory_store final : public raft_storage`

完整实现 `raft_storage` 接口，数据存储在内存中。适用于测试和非持久化场景。

### `leveldb_store` — LevelDB 持久化存储

**签名**: `export class leveldb_store final : public raft_storage`（需 `CNETMOD_HAS_LEVELDB`）

```cpp
explicit leveldb_store(std::string path);
void set_sync(bool enabled) noexcept;
```

### `state_machine` — 有限状态机接口

**签名**: `export class state_machine`

| 方法 | 签名 | 说明 |
|------|------|------|
| `on_apply` | `virtual void on_apply(const log_entry&) = 0` | 应用日志条目（必须实现） |
| `on_snapshot_save` | `virtual void on_snapshot_save(const snapshot_metadata&)` | 快照保存回调 |
| `on_snapshot_load` | `virtual void on_snapshot_load(const snapshot_metadata&)` | 快照加载回调 |
| `save_snapshot` | `virtual auto save_snapshot(const snapshot_writer&) -> std::expected<void, raft_error>` | 保存快照数据 |
| `load_snapshot` | `virtual auto load_snapshot(const snapshot_reader&) -> std::expected<void, raft_error>` | 加载快照数据 |
| `on_leader_start` | `virtual void on_leader_start(term_t)` | Leader 上任回调 |
| `on_leader_stop` | `virtual void on_leader_stop(term_t)` | Leader 卸任回调 |

### `log_manager` — 日志管理

**签名**: `export class log_manager`

```cpp
explicit log_manager(std::shared_ptr<raft_storage> storage);
auto append_as_leader(term_t, std::string command) -> log_entry;
auto append_noop(term_t) -> log_entry;
auto append_configuration(term_t, configuration_state) -> log_entry;
auto append_from_leader(log_index prev, term_t prev_term, const std::vector<log_entry>&) -> append_result;
auto restore_snapshot(const snapshot_metadata&) -> append_result;
void compact_prefix(log_index first_kept);
```

### `progress_tracker` — 复制进度跟踪

**签名**: `export class progress_tracker`

```cpp
progress_tracker(std::vector<node_id> peers, log_index next_index, std::size_t inflight_capacity);
auto get(const node_id& peer) -> peer_progress*;
void mark_sent(const node_id& peer, log_index last_index);
void mark_replicated(const node_id& peer, log_index index);
void mark_rejected(const node_id& peer, log_index rejected_next, log_index conflict = 0);
auto committed_index(log_index leader_match, std::size_t majority) const -> log_index;
```

### `configuration` — 集群配置管理

**签名**: `export class configuration`

| 方法 | 说明 |
|------|------|
| `contains(node_id)` | 是否为投票成员 |
| `is_member(node_id)` | 是否为任意成员 |
| `quorum_size()` | 法定人数 |
| `has_quorum(set<node_id>)` | 是否达到法定人数 |
| `with_joint(new_voters)` | 进入联合配置变更 |
| `with_learners(learners)` | 添加 Learner |
| `promote_learner(node_id)` | 提升 Learner 为 Voter |
| `remove_member(node_id)` | 移除成员 |
| `leave_joint()` | 完成联合配置 |

### `raft_tcp_transport` — TCP 传输层

**签名**: `export class raft_tcp_transport final : public raft_transport`

```cpp
struct raft_tcp_transport_options {
    std::uint32_t max_send_attempts = 3;
    std::chrono::milliseconds retry_backoff{20};
    std::filesystem::path snapshot_directory = "raft-snapshots";
    std::size_t snapshot_chunk_size = 1024 * 1024;
    raft_tcp_security_options security;
    raft_snapshot_retention_options snapshot_retention;
};
```

| 方法 | 签名 | 说明 |
|------|------|------|
| 构造 | `raft_tcp_transport(io_context&, node_id local_id, options)` | |
| `set_node` | `void set_node(raft_node&) noexcept` | 绑定 Raft 节点 |
| `add_peer` | `void add_peer(raft_tcp_peer)` | 添加对端 |
| `serve` | `auto serve(endpoint) -> task<std::expected<void, std::error_code>>` | 监听入站连接 |
| `broadcast_pre_vote` | `void broadcast_pre_vote(raft_node&)` | 广播 Pre-Vote |
| `broadcast_request_vote` | `void broadcast_request_vote(raft_node&)` | 广播投票请求 |
| `replicate_to_all` | `void replicate_to_all(raft_node&)` | 向所有 Follower 复制日志 |
| `transfer_leader` | `auto transfer_leader(raft_node&, const node_id&) -> std::expected<void, raft_error>` | 转移 Leader |
| `cleanup_snapshot_files` | `auto cleanup_snapshot_files() -> task<std::expected<std::size_t, std::error_code>>` | 清理过期快照 |
| `stop` / `async_stop` | | 停止传输层 |

### `raft_node_runtime` — 运行时

**签名**: `export class raft_node_runtime`

```cpp
struct raft_runtime_options {
    bool start_tcp_server = true;
    bool auto_election = true;
    bool auto_heartbeat = true;
    bool auto_snapshot = false;
    raft_snapshot_policy snapshot_policy;
};
```

| 方法 | 签名 | 说明 |
|------|------|------|
| 构造 | `raft_node_runtime(io_context&, raft_node&, raft_tcp_transport&, endpoint, raft_options, runtime_options)` | |
| `start` | `void start()` | 启动运行时（TCP + 选举 + 心跳） |
| `stop` | `void stop() noexcept` | 停止 |
| `async_stop` | `auto async_stop() -> task<void>` | 异步停止 |
| `tick_now` | `void tick_now()` | 立即触发一次 tick |
| `transfer_leader` | `auto transfer_leader(const node_id&) -> std::expected<void, raft_error>` | 转移 Leader |
| `maybe_snapshot_now` | `auto maybe_snapshot_now() -> std::expected<std::optional<snapshot_metadata>, raft_error>` | 手动触发快照 |
| `async_read_index` | `auto async_read_index(read_index_request) -> task<std::expected<read_index_response, raft_error>>` | 线性一致性读 |

## Do's & Don'ts

- **Do**: 实现 `state_machine::on_apply` 来应用日志命令，这是唯一必须实现的虚函数
- **Do**: 生产环境使用 `leveldb_store` 持久化，测试用 `memory_store`
- **Do**: 配置 `raft_snapshot_policy` 定期快照以压缩日志
- **Do**: 使用 `raft_node_runtime` 简化选举/心跳/快照的自动化管理
- **Don't**: 不要在非 Leader 节点调用 `append_command`，会返回 `raft_errc::not_leader`
- **Don't**: 不要忽略 `raft_tcp_transport::serve`，节点必须监听才能接收 RPC

## 场景：创建 Raft 集群

```cpp
import std;
import cnetmod.core.address;
import cnetmod.core.net_init;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.protocol.raft;

namespace raft = cnetmod::raft;

auto run_node(cnetmod::io_context& ctx, std::string id,
    std::vector<std::string> peers, std::uint16_t port) -> cnetmod::task<void>
{
    raft::raft_config cfg{.id = id, .peers = peers};
    auto store = std::make_shared<raft::memory_store>();
    raft::raft_node node(cfg, store, /*state_machine=*/nullptr);

    raft::raft_tcp_transport transport(ctx, id);
    transport.set_node(node);
    transport.add_peer({.id = peers[0], .address = cnetmod::endpoint{
        cnetmod::ip_address{cnetmod::ipv4_address{127,0,0,1}},
        static_cast<std::uint16_t>(port + 1)}});

    raft::raft_node_runtime runtime(ctx, node, transport,
        cnetmod::endpoint{cnetmod::ip_address{cnetmod::ipv4_address::any()}, port},
        cfg.options);
    runtime.start();

    // 等待直到停止
    co_await cnetmod::async_sleep(ctx, std::chrono::seconds(30));
    runtime.stop();
}
```

## 场景：自定义状态机

```cpp
class kv_state_machine final : public raft::state_machine {
public:
    void on_apply(const raft::log_entry& entry) override {
        if (entry.type != raft::entry_type::command) return;
        // 解析命令并应用到 KV 存储
        std::istringstream ss(entry.command);
        std::string op, key, value;
        ss >> op >> key >> value;
        if (op == "SET") data_[key] = value;
        else if (op == "DEL") data_.erase(key);
    }

    auto save_snapshot(const raft::snapshot_writer& writer)
        -> std::expected<void, raft::raft_error> override {
        // 序列化 data_ 到文件
        return {};
    }

    auto load_snapshot(const raft::snapshot_reader& reader)
        -> std::expected<void, raft::raft_error> override {
        // 从文件反序列化 data_
        return {};
    }

    void on_leader_start(raft::term_t term) override {
        std::println("Became leader at term {}", term);
    }

private:
    std::map<std::string, std::string> data_;
};
```

## 场景：持久化存储

```cpp
// LevelDB 持久化（需 -DCNETMOD_ENABLE_LEVELDB=ON）
if constexpr (raft::leveldb_store_available) {
    auto store = std::make_shared<raft::leveldb_store>("/var/raft/node1");
    store->set_sync(true); // 每次写都 fsync
    raft::raft_node node(cfg, store, &fsm);
}

// 内存存储（测试用）
auto store = std::make_shared<raft::memory_store>();
raft::raft_node node(cfg, store, &fsm);
```

## 场景：快照机制

```cpp
// 配置自动快照策略
raft::raft_snapshot_policy policy{
    .log_entries_threshold = 10000,  // 每 10000 条日志触发
    .min_interval = std::chrono::milliseconds{60000},
    .uri_prefix = "raft-snapshot"
};

// 运行时自动快照
raft::raft_runtime_options runtime_opts{
    .auto_snapshot = true,
    .snapshot_policy = policy
};
raft::raft_node_runtime runtime(ctx, node, transport, ep, cfg.options, runtime_opts);

// 手动触发快照
auto result = runtime.maybe_snapshot_now();
if (result && *result) {
    std::println("Snapshot created at index {}", (*result)->last_included_index);
}
```

## 参考示例

- `examples/raft/redis_cluster.cpp` — Redis 风格 KV 复制集群
- `examples/raft/oss_shared_storage.cpp` — OSS 风格对象存储（含分片）
- `examples/raft/raft_demo_cluster.hpp` — 三节点集群测试工具

## 连接池/连接管理（生产级用法）

### 说明

Raft 是分布式共识协议，**不适用传统连接池概念**。`raft_tcp_transport` 内部为每个 peer 维护独立的 TCP 长连接（`peer_connection`），自动管理重连、消息队列和发送重试：

```cpp
// raft_tcp_transport — 内部连接管理
export class raft_tcp_transport final : public raft_transport {
    raft_tcp_transport(io_context& ctx, node_id local_id,
        raft_tcp_transport_options options = {});

    void add_peer(raft_tcp_peer peer);          // 添加对端（自动建连）
    void remove_peer(const node_id& peer);      // 移除对端
    auto peers() const -> std::vector<node_id>;

    // 传输层指标（每 peer 独立统计）
    auto peer_metrics() const -> std::vector<raft_peer_transport_metrics>;
    auto peer_metrics(const node_id& peer) const
        -> std::optional<raft_peer_transport_metrics>;
};

// 传输选项 — 控制连接行为
struct raft_tcp_transport_options {
    std::uint32_t max_send_attempts = 3;        // 最大发送重试
    std::chrono::milliseconds retry_backoff{20}; // 重试退避
    std::size_t max_outbound_queue = 1024;       // 每 peer 最大出站队列
    raft_tcp_security_options security;          // TLS/认证
    raft_snapshot_retention_options snapshot_retention;
};

// 每 peer 传输指标
struct raft_peer_transport_metrics {
    node_id peer;
    bool connected = false;
    std::uint64_t queued_sends = 0;
    std::uint64_t send_successes = 0;
    std::uint64_t send_failures = 0;
    std::uint64_t reconnects = 0;
    std::uint64_t max_queue_depth = 0;
    std::chrono::steady_clock::duration last_queue_wait_latency{};
    std::chrono::steady_clock::duration last_send_latency{};
    std::chrono::steady_clock::time_point last_send_at{};
    std::chrono::steady_clock::time_point last_receive_at{};
    std::error_code last_error;
};
```

### 传输层连接监控

```cpp
import std;
import cnetmod.protocol.raft;

namespace raft = cnetmod::raft;

// 监控所有 peer 连接状态
void print_transport_metrics(const raft::raft_tcp_transport& transport) {
    auto metrics = transport.peer_metrics();
    for (auto& m : metrics) {
        std::println("Peer: {} | connected={} | successes={} | failures={} | "
                     "reconnects={} | queue={}",
            m.peer, m.connected, m.send_successes, m.send_failures,
            m.reconnects, m.queued_sends);
    }
}
```

## 多核/集群部署

### 部署模式

Raft 是分布式共识协议，"多核" 的含义是**多节点集群**而非多线程 worker。每个 Raft 节点运行在独立的 `io_context` 上，通过 `raft_tcp_transport` 互联。`raft_node_runtime` 自动管理选举、心跳和快照。

**模块不使用 `server_context`**，每个节点独立部署在单线程或多线程 `io_context` 上。

### 关键 API

```cpp
// raft_node_runtime — 自动化运行时
export class raft_node_runtime {
    raft_node_runtime(io_context& ctx, raft_node& node,
        raft_tcp_transport& transport, endpoint listen_endpoint,
        raft_options options,
        raft_runtime_options runtime_options = {});

    void start();    // 启动 TCP 服务 + 选举循环 + 心跳循环
    void stop() noexcept;
    auto async_stop() -> task<void>;
    void tick_now();
    auto transfer_leader(const node_id& target) -> std::expected<void, raft_error>;
    auto maybe_snapshot_now() -> std::expected<std::optional<snapshot_metadata>, raft_error>;
    auto async_read_index(read_index_request request)
        -> task<std::expected<read_index_response, raft_error>>;
};

// raft_runtime_options — 运行时控制
struct raft_runtime_options {
    bool start_tcp_server = true;   // 自动启动 TCP 监听
    bool auto_election = true;      // 自动发起选举
    bool auto_heartbeat = true;     // 自动发送心跳
    bool auto_snapshot = false;     // 自动快照（生产建议开启）
    raft_snapshot_policy snapshot_policy;
};
```

### 生产级三节点集群

```cpp
import std;
import cnetmod.core;
import cnetmod.io;
import cnetmod.coro;
import cnetmod.executor;
import cnetmod.protocol.raft;

namespace cn = cnetmod;
namespace raft = cn::raft;

// KV 状态机（生产级）
class kv_state_machine final : public raft::state_machine {
public:
    void on_apply(const raft::log_entry& entry) override {
        if (entry.type != raft::entry_type::command) return;
        auto pos = entry.command.find(' ');
        if (pos == std::string::npos) return;
        auto op = entry.command.substr(0, pos);
        auto rest = entry.command.substr(pos + 1);

        if (op == "SET") {
            auto eq = rest.find('=');
            if (eq != std::string::npos)
                data_[rest.substr(0, eq)] = rest.substr(eq + 1);
        } else if (op == "DEL") {
            data_.erase(rest);
        }
    }

    auto save_snapshot(const raft::snapshot_writer& writer)
        -> std::expected<void, raft::raft_error> override {
        std::ofstream out(writer.uri, std::ios::binary);
        for (auto& [k, v] : data_) {
            std::uint32_t ks = static_cast<std::uint32_t>(k.size());
            std::uint32_t vs = static_cast<std::uint32_t>(v.size());
            out.write(reinterpret_cast<const char*>(&ks), 4);
            out.write(k.data(), ks);
            out.write(reinterpret_cast<const char*>(&vs), 4);
            out.write(v.data(), vs);
        }
        return {};
    }

    auto load_snapshot(const raft::snapshot_reader& reader)
        -> std::expected<void, raft::raft_error> override {
        data_.clear();
        std::ifstream in(reader.uri, std::ios::binary);
        while (in) {
            std::uint32_t ks, vs;
            if (!in.read(reinterpret_cast<char*>(&ks), 4)) break;
            std::string k(ks, '\0');
            in.read(k.data(), ks);
            in.read(reinterpret_cast<char*>(&vs), 4);
            std::string v(vs, '\0');
            in.read(v.data(), vs);
            data_[k] = v;
        }
        return {};
    }

    void on_leader_start(raft::term_t term) override {
        std::println("成为 Leader, term={}", term);
    }
    void on_leader_stop(raft::term_t term) override {
        std::println("失去 Leader, term={}", term);
    }

    auto get(const std::string& key) const -> std::optional<std::string> {
        auto it = data_.find(key);
        return it != data_.end() ? std::optional{it->second} : std::nullopt;
    }

private:
    std::map<std::string, std::string> data_;
};

// 集群节点配置
struct cluster_node_config {
    std::string id;
    std::uint16_t port;
    std::vector<std::string> peer_ids;
    std::vector<std::pair<std::string, std::uint16_t>> peer_addrs;
};

auto run_cluster_node(cn::io_context& ctx, cluster_node_config cfg)
    -> cn::task<void>
{
    // 1. 配置 Raft 选项
    raft::raft_config raft_cfg{
        .id = cfg.id,
        .peers = cfg.peer_ids,
        .options = {
            .election_timeout = std::chrono::milliseconds(300),
            .heartbeat_interval = std::chrono::milliseconds(100),
            .leader_lease_timeout = std::chrono::milliseconds(200),
            .pre_vote = true,
            .check_quorum = true,
            .max_entries_per_append = 256,
        },
    };

    // 2. 持久化存储（生产环境用 LevelDB）
    auto store = std::make_shared<raft::leveldb_store>(
        std::format("/var/raft/{}", cfg.id));
    store->set_sync(true);

    // 3. 状态机
    kv_state_machine fsm;

    // 4. 创建节点
    raft::raft_node node(raft_cfg, store, &fsm);

    // 5. TCP 传输层
    raft::raft_tcp_transport_options transport_opts;
    transport_opts.max_send_attempts = 5;
    transport_opts.retry_backoff = std::chrono::milliseconds(50);
    transport_opts.snapshot_directory = std::format("/var/raft/{}/snapshots", cfg.id);
    transport_opts.snapshot_chunk_size = 2 * 1024 * 1024;  // 2MB 分块
    transport_opts.max_outbound_queue = 2048;

    // TLS 安全配置（生产环境建议开启）
    transport_opts.security.shared_secret = "raft-cluster-secret-key";
    transport_opts.security.require_auth_token = true;

    raft::raft_tcp_transport transport(ctx, cfg.id, transport_opts);
    transport.set_node(node);

    // 添加所有 peer
    for (std::size_t i = 0; i < cfg.peer_ids.size(); ++i) {
        auto& [peer_host, peer_port] = cfg.peer_addrs[i];
        transport.add_peer({
            .id = cfg.peer_ids[i],
            .address = cn::endpoint{
                cn::ip_address{cn::ipv4_address::from_string(peer_host)},
                peer_port},
        });
    }

    // 6. 运行时（自动选举 + 心跳 + 快照）
    raft::raft_snapshot_policy snap_policy{
        .log_entries_threshold = 10000,
        .min_interval = std::chrono::milliseconds(60000),
        .uri_prefix = std::format("raft-{}-snap", cfg.id),
    };

    raft::raft_runtime_options runtime_opts{
        .start_tcp_server = true,
        .auto_election = true,
        .auto_heartbeat = true,
        .auto_snapshot = true,
        .snapshot_policy = snap_policy,
    };

    auto listen_ep = cn::endpoint{
        cn::ip_address{cn::ipv4_address::any()}, cfg.port};

    raft::raft_node_runtime runtime(ctx, node, transport, listen_ep,
        raft_cfg.options, runtime_opts);

    runtime.start();
    std::println("节点 {} 启动, 监听端口 {}", cfg.id, cfg.port);

    // 7. 定期监控节点状态
    while (runtime.running()) {
        co_await cn::async_sleep(ctx, std::chrono::seconds(10));
        auto m = node.metrics();
        std::println("[{}] role={} term={} commit={} applied={} voters={} "
                     "learners={} pending_reads={}",
            cfg.id, raft::role_name(m.role), m.current_term,
            m.commit_index, m.last_applied, m.voters, m.learners,
            m.pending_reads);

        // 清理过期快照文件
        auto cleaned = co_await transport.cleanup_snapshot_files();
        if (cleaned && *cleaned > 0) {
            std::println("清理 {} 个过期快照", *cleaned);
        }
    }

    co_await runtime.async_stop();
}

// 启动三节点集群（同一进程内，生产环境应分进程/机器部署）
auto main() -> int {
    cn::net_init net;

    std::vector<cluster_node_config> nodes = {
        {"node1", 9001, {"node2", "node3"},
         {{"127.0.0.1", 9002}, {"127.0.0.1", 9003}}},
        {"node2", 9002, {"node1", "node3"},
         {{"127.0.0.1", 9001}, {"127.0.0.1", 9003}}},
        {"node3", 9003, {"node1", "node2"},
         {{"127.0.0.1", 9001}, {"127.0.0.1", 9002}}},
    };

    // 每个节点独立 io_context（生产环境应分进程）
    std::vector<std::thread> threads;
    for (auto& cfg : nodes) {
        threads.emplace_back([&cfg]() {
            cn::io_context ctx;
            cn::spawn(ctx, run_cluster_node(ctx, cfg));
            ctx.run();
        });
    }

    for (auto& t : threads) t.join();
    return 0;
}
```

### 线性一致性读（ReadIndex）

```cpp
// 通过 ReadIndex 实现线性一致性读（无需写入日志）
auto read_with_consistency(raft::raft_node_runtime& runtime,
    cn::io_context& ctx) -> cn::task<void>
{
    raft::read_index_request req{
        .id = 1,
        .context = "read-user-data",
    };

    auto result = co_await runtime.async_read_index(req);
    if (result) {
        std::println("ReadIndex: term={} index={} ready={}",
            result->term, result->index, result->ready);
        // ready=true 时可安全读取本地状态机
    }
}
```

### Leader 转移

```cpp
// 优雅下线前转移 Leader 到其他节点
auto graceful_shutdown(raft::raft_node_runtime& runtime) -> void {
    auto r = runtime.transfer_leader("node2");
    if (r) {
        std::println("Leader 转移已发起到 node2");
    } else {
        std::println("Leader 转移失败: {}", r.error().message);
    }
}
```
