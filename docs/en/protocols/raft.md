# Raft

cnetmod provides a Raft replicated state machine stack for strongly consistent
services. It is intended to be used as a library component, not as a demo-only
helper: election, log replication, storage, snapshot transfer, membership,
linear reads, TCP transport, TLS/authentication hooks, metrics, and runtime
loops are separated behind explicit interfaces.

Use Raft when all replicas must apply the same ordered command stream, for
example metadata services, configuration stores, primary-backup control planes,
small distributed key-value databases, or sharded storage metadata. Do not put
large object payloads directly into Raft logs in production; store large blobs
outside Raft and replicate only metadata, version, ownership, and delete order.

## Module Map

Import the aggregate module for normal use:

```cpp
import cnetmod.protocol.raft;
```

The implementation is split into these module partitions:

| Partition | Responsibility |
|-----------|----------------|
| `types` | Stable public types: terms, log indexes, roles, log entries, hard state, RPC records, errors, metrics, and options. |
| `configuration` | Voter / learner sets, quorum calculation, joint consensus, and majority checks. |
| `storage` | Durable storage interface for hard state, snapshot metadata, and log entries. |
| `memory_store` | In-memory storage for tests, examples, and embedded non-durable deployments. |
| `leveldb_store` | LevelDB-backed durable storage, batched log writes, restart recovery, optional sync writes. |
| `log_manager` | Log matching, append, conflict truncation, commit advancement, compaction, and snapshot restore. |
| `progress` | Per-follower replication progress with `probe`, `replicate`, `snapshot`, and inflight backpressure. |
| `fsm` | User state-machine apply and snapshot save/load callbacks. |
| `node` | Raft state machine: elections, leader transitions, replication decisions, ReadIndex, membership, snapshots, and leader transfer. |
| `wire` | Binary Raft RPC frame codec and corruption/truncation validation. |
| `transport` | Abstract transport interface used by `raft_node`. |
| `tcp_transport` | Real TCP transport, long-lived peer connections, auth/TLS hooks, snapshot chunk transfer, retries, queues, and metrics. |
| `runtime` | Runtime loops for TCP serving, randomized election ticks, heartbeat/replication ticks, automatic snapshots, cleanup, and cancellation. |

## Public Model

Important public types:

| Type | Purpose |
|------|---------|
| `raft_config` | Local node id, peer ids, and `raft_options`. |
| `raft_options` | Election timeout, heartbeat interval, leader lease, ReadIndex timeout, pre-vote, check-quorum, append batching, inflight window, snapshot chunk size. |
| `raft_node` | Deterministic Raft core. Can be tested in-process or driven by a transport/runtime. |
| `raft_node_runtime` | Runs TCP server, election loop, heartbeat/replication loop, optional automatic snapshot policy. |
| `raft_tcp_transport` | Sends and receives Raft RPC over TCP; owns peer addresses, connection reuse, retry/backoff, security and metrics. |
| `raft_storage` | Storage interface. Implement it yourself or use `memory_store` / `leveldb_store`. |
| `state_machine` | Override `on_apply`, `save_snapshot`, and `load_snapshot` to apply committed commands and materialize snapshots. |
| `raft_metrics` | Node state, log indexes, applied index, snapshot index, voter/learner counts, pending reads, active followers, fatal error. |
| `raft_peer_transport_metrics` | Per-peer queued sends, successes, failures, reconnects, last send/receive time, latency, last error. |

## Basic Topology

Each Raft node needs a stable id, a peer list, durable storage, a state machine,
a TCP transport, and a listen endpoint.

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
        // Decode entry.command and mutate local state.
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

For a real networked node, configure peer endpoints and start the runtime:

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

For complete runnable examples with loopback TCP ports, see:

- `examples/raft/redis_cluster.cpp`
- `examples/raft/oss_shared_storage.cpp`
- `examples/raft/raft_demo_cluster.hpp`

## Write Path

The normal write path is:

1. The service accepts a business command on the current leader.
2. The leader calls `raft_node::append_command(command)`.
3. `raft_tcp_transport::replicate_to_all(node)` sends AppendEntries or snapshot
   install RPCs to followers.
4. Followers validate term, previous log index/term, append entries, update
   commit index, and reply.
5. The leader advances commit after quorum acknowledgement.
6. `state_machine::on_apply(entry)` is called in log order.

`append_command` returns `raft_errc::not_leader` if the local node is not the
leader. A production service should route writes to the known leader or return a
redirect/retry signal to its client. The current leader id is visible through
`raft_node::leader_id()` and `raft_metrics::leader_id`.

## Linear Reads

cnetmod supports ReadIndex-style linear reads:

```cpp
auto response = co_await runtime.async_read_index(raft::read_index_request{
    .id = next_request_id(),
    .context = "metadata-read",
});

if (!response)
    co_return response.error();

// It is now safe to serve a read from the local state machine after the local
// applied index has reached response->index.
```

Behavior:

- Single-node leaders can return a ready ReadIndex immediately.
- Multi-node leaders attach read contexts to heartbeat / AppendEntries and wait
  for quorum acknowledgement.
- Pending reads expire using `raft_options::read_index_timeout`.
- Pending ReadIndex requests are cleared when the node steps down.
- Lease reads require `check_quorum = true`; the code rejects unsafe
  `lease_read = true` with `check_quorum = false`.

For serving reads on followers, gate reads on both a valid ReadIndex response
and local apply progress. Do not serve stale follower state without an
application-level stale-read contract.

## Elections, Lease, and Check-Quorum

Defaults are conservative:

- `pre_vote = true` avoids disruptive term increments by isolated nodes.
- `check_quorum = true` forces a leader to step down if it stops hearing from a
  majority within the leader lease timeout.
- `leader_lease_timeout` controls the quorum activity window used by
  `leader_lease_valid()` and `check_leader_quorum()`.

Recommended timing rule:

```text
heartbeat_interval < leader_lease_timeout < election_timeout
```

In unstable networks, use a larger election timeout and keep heartbeat intervals
small enough to detect loss promptly without overwhelming slow peers.

## Membership Changes

The API exposes:

- `set_learners(learners)`
- `promote_learner(id)`
- `enter_joint_configuration(new_voters)`
- `leave_joint_configuration()`
- `remove_node(id)`
- `transfer_leader(target)`

Operational rules:

- Add a new node as a learner first.
- Wait until it catches up.
- Promote it to voter through joint consensus.
- Remove old voters only after the new configuration is committed.
- If the leader removes itself, it steps down after the committed leave-joint
  transition.
- Leader transfer sends `TimeoutNow` immediately if the target is caught up;
  otherwise replication is triggered first and `TimeoutNow` is sent when the
  target catches up.

Learners replicate logs but do not count toward quorum and cannot start a valid
election.

## Snapshots and Compaction

Snapshot data has two layers:

- `snapshot_metadata`: last included index/term, URI, and configuration.
- Snapshot payload: application state materialized by `state_machine`.

Manual snapshot:

```cpp
auto snapshot = node.create_snapshot("data/raft-n1/snapshots/n1-100.snapshot");
if (!snapshot)
    handle(snapshot.error());
```

Automatic snapshot:

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

Transport behavior:

- If `install_snapshot_request::uri` is set and `data` is empty, the TCP
  transport streams the file in chunks.
- If `data` is larger than the configured chunk size, it is also chunked.
- Each chunk carries a CRC32; the full file carries a CRC32.
- Followers write to a temporary file, validate offsets and checksums, then move
  the completed file into the snapshot directory.
- If a receiver reports an accepted offset, the sender can resume the transfer.

Retention:

```cpp
raft::raft_tcp_transport_options{
    .snapshot_directory = "data/raft-n1/snapshots",
    .snapshot_retention = raft::raft_snapshot_retention_options{
        .keep_last = 3,
        .min_age = std::chrono::minutes{10},
    },
};
```

`raft_node_runtime` calls `cleanup_snapshot_files()` from the heartbeat loop
when it is running as leader. Cleanup enumerates `.snapshot` files in the
transport snapshot directory and removes old files beyond `keep_last` after
`min_age`.

## Storage

Use `memory_store` only for tests, examples, and explicitly non-durable
deployments. For recovery across process restarts, use `leveldb_store`:

```cpp
auto store = std::make_shared<raft::leveldb_store>("data/raft-n1/leveldb");
store->set_sync(true); // if your durability policy requires synchronous writes
```

The storage interface persists:

- current term, vote, commit index, and last applied index;
- snapshot metadata;
- log entries;
- prefix/suffix truncation;
- reset-to-snapshot state.

If storage throws during critical operations, `raft_node` transitions to a
stopped state and exposes the fatal error through `fatal_error()` and metrics.

## TCP Transport

`raft_tcp_transport` implements the real network path:

- accepts peer connections with `serve(endpoint, cancel_token)`;
- encodes and decodes binary Raft frames;
- dispatches requests to `raft_node`;
- sends responses on the same transport path;
- reuses long-lived peer connections;
- retries with `max_send_attempts` and `retry_backoff`;
- applies `max_outbound_queue` backpressure;
- switches to snapshot install when a follower is behind compacted logs;
- records per-peer health and latency metrics.

Inspect transport state with:

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

## Authentication and TLS

Transport security is configured in `raft_tcp_security_options`:

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

Notes:

- `shared_secret` protects the Raft frame authentication token.
- TLS requires configured `ssl_context` objects when OpenSSL support is enabled.
- With mTLS, peer certificate SHA-256 fingerprints can be pinned per node id.
- In production, keep node ids, peer addresses, certificates, and shared
  secrets in a controlled cluster configuration source.

## Error Handling

Most mutating APIs return `std::expected<T, raft_error>`. Common error codes:

| Code | Meaning |
|------|---------|
| `stale_term` | Request/response term is obsolete. |
| `log_inconsistent` | Log matching failed or committed entry is missing. |
| `not_leader` | Local node cannot accept leader-only operation. |
| `not_voter` | Operation requires a voting member. |
| `configuration_error` | Invalid or concurrent membership change. |
| `snapshot_required` | Follower needs snapshot instead of normal append. |
| `storage_error` | Durable store or snapshot file operation failed. |
| `state_machine_error` | User state machine apply/snapshot callback failed. |
| `backpressure` | Transport queue/inflight window is full. |
| `stopped` | Node/runtime is stopped or request timed out. |

Install a transport error handler for operational visibility:

```cpp
transport.set_error_handler([](raft::node_id peer, std::error_code ec) {
    std::println(stderr, "raft peer {} error: {}", peer, ec.message());
});
```

## Distributed Storage Examples

The repository includes two TCP-backed service examples.

`examples/raft/redis_cluster.cpp`:

- starts a three-node loopback TCP Raft cluster on ports `19101..19103`;
- exposes Redis-style service methods: `SET`, `DEL`, `INCR`;
- commits business commands through Raft and applies the same command stream on
  all replicas;
- demonstrates TTL handling in the state machine.

`examples/raft/oss_shared_storage.cpp`:

- demonstrates an OSS-style metadata service;
- runs a primary-backup replica set on `19101..19103`;
- runs a two-shard namespace using two independent three-node Raft groups on
  `19201..19203` and `19301..19303`;
- replicates object metadata, etag/version updates, and delete order.

Build targets are listed in `examples/CMakeLists.txt`:

```bash
cmake --build <build-dir> --target example_redis_cluster --config Release
cmake --build <build-dir> --target example_oss_shared_storage --config Release
```

## Testing

Primary coverage is in `testing/tests/test_raft.cpp`. It covers:

- single-node and majority election/commit;
- stale votes and PreVote;
- AppendEntries matching, conflicts, and retries;
- binary wire codec round trips and corrupt frame rejection;
- snapshot install, snapshot metadata, chunk metadata, resume metadata, and
  follower catch-up after compaction;
- state-machine snapshot save/load;
- joint consensus, concurrent configuration rejection, learner replication,
  learner promotion, node removal, and leader self-removal;
- leader lease, check-quorum, lease-read constraints, and ReadIndex timeouts;
- leader transfer and TimeoutNow;
- storage failure and snapshot failure stop behavior;
- transport backpressure, inflight window, retry, and peer metrics;
- snapshot retention cleanup;
- auth token rejection, TLS misconfiguration, and mTLS certificate pinning;
- real three-node TCP loopback replication;
- five-node partition/heal and seeded chaos/fuzz convergence;
- LevelDB hard-state and log recovery after restart.

Run:

```bash
cmake --build <build-dir> --target test_raft --config Release
ctest --test-dir <build-dir> -C Release -R test_raft
```

## Performance

The repository includes an in-process deterministic Raft core benchmark:

```bash
cmake --build <build-dir> --target bench_raft --config Release
<build-dir>/testing/bench/bench_raft
```

On Windows Visual Studio generators, the executable is usually under
`<build-dir>/testing/bench/Release/bench_raft.exe`.

Measured on Intel Core i9-14900K in Release:

| Scenario | Throughput |
|----------|------------|
| Single-node append + commit | ~5.17M - 5.50M ops/s |
| 5-node majority replication | ~0.80M - 0.83M ops/s |

These are core benchmark numbers, not WAN or disk-fsync end-to-end numbers.
Actual service throughput depends on compiler, allocator, CPU frequency policy,
storage sync policy, command size, state-machine work, network latency,
snapshot policy, and TLS/auth settings.

## Production Checklist

Before deploying a Raft-backed service:

- Use stable node ids and stable persistent directories.
- Use `leveldb_store` or another durable `raft_storage`; do not use
  `memory_store` for durable clusters.
- Keep Raft logs on reliable storage and choose `leveldb_store::set_sync`
  according to your durability/latency budget.
- Keep `heartbeat_interval`, `leader_lease_timeout`, and `election_timeout`
  consistent across nodes.
- Enable `pre_vote` and `check_quorum` for production clusters.
- Configure snapshot policy, snapshot directory, chunk size, and retention.
- Implement state-machine `save_snapshot` and `load_snapshot` for fast recovery.
- Protect peer traffic with auth token and TLS/mTLS when nodes cross trust
  boundaries.
- Export `raft_metrics` and `raft_peer_transport_metrics`.
- Test restart recovery, disk-full/storage errors, leader transfer,
  membership changes, network partitions, snapshot install, and rolling restart.
- Treat application command encoding as a stable wire/storage format; version it
  before incompatible changes.

## Current Boundary

The Raft stack is broad enough for real service integration, but it is still a
library layer. Your application remains responsible for:

- client protocol and leader redirect behavior;
- durable command encoding/versioning;
- snapshot payload format and compatibility;
- cluster configuration distribution;
- metrics export format;
- operational automation for certificate rotation, backup, restore, and rolling
  upgrades.
