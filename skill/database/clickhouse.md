# ClickHouse Native TCP, Application and ORM

> Executor-owned coroutine client, checksummed compression, managed connection
> pools and provider-neutral repositories for ClickHouse analytical SQL.

**Protocol import:** `import cnetmod.protocol.clickhouse;`
**ORM adapter:** `import cnetmod.protocol.clickhouse.orm;`
**Application:** `import cnetmod.application;`
**CMake:** `-DCNETMOD_ENABLE_CLICKHOUSE=ON`; add HTTP and ORM for Application repositories.
**Targets:** `cnetmod::clickhouse`, `cnetmod::application`, `cnetmod::orm`.

## Responsibility boundaries

- `column/`: recursive type grammar, column-major encoding and LowCardinality dictionaries.
- `block/`: BlockInfo, row/column counts and per-column serialization flags.
- `compression/`: Native frames, CityHash 1.0.2, LZ4 and Zstandard.
- `wire/`: bounded primitive codecs and version-aware server packet parsing.
- `transport/`: cancellable TCP/TLS reads/writes and operation deadlines.
- `client/`: connect/hello, SELECT, server-side Cancel and schema-checked INSERT.
- `pool/`: bounded executor-owned connection leases and shutdown admission.
- `orm/`: server-bound SQL parameters, native result adaptation and session gateways.
- `src/application/integration/clickhouse*`: configuration, loop-local pools,
  lifecycle, health and repository factory binding.

The public protocol module declares contracts; non-template implementations are
split by responsibility. Do not introduce `types.cpp`/`types.cppm` dumping grounds.
Protocol code must not import Application. Generic ORM policy stays provider-neutral;
ClickHouse SQL/wire adaptation stays in the ClickHouse adapter.

## Protocol capabilities

The default Native revision is **54459**, with settings, typed parameters,
LowCardinality, custom-serialization markers, logs/profile events, INSERT metadata
and the handshake addendum. The explicit legacy **54401** profile remains available
through `connection_options::revision`; legacy servers materialize LowCardinality
results and do not support the modern settings/parameter API. Revisions outside
these implemented profiles are rejected. A server's advertised version is not used
to guess fields newer than the negotiated client revision.

| Feature | Behavior |
|---|---|
| Compression | Default LZ4; ZSTD, checksum-only and uncompressed selectable |
| Integrity | Every compressed frame validates CityHash **1.0.2** before decompression |
| Framing | Blocks may span multiple independently checksummed frames |
| Resource bounds | Compressed/uncompressed bytes, strings, rows, columns, value nodes and nesting |
| Query | ID, settings, server-bound `{name:Type}` parameters, external tables |
| Results | Data/schema blocks, totals/extremes, progress, profile, logs/profile events |
| INSERT | Quoted identifiers, exact sample schema check, Native data blocks, EndOfStream |
| Streaming | Awaited block sink with backpressure; no retained data blocks |
| Cancellation | Transport cancellation and Native Cancel for an active SELECT |
| TLS | Direct Native TLS with CA and hostname verification; system roots or explicit CA |
| Failure | Structured code/name/message/nested exceptions; no credential/stack-trace logging |

LZ4 and Zstandard development libraries are required when enabling ClickHouse.
The exact historical CityHash source is vendored with its MIT notice and pinned
upstream commit; a modern system CityHash is not interchangeable. The SDK installs
its third-party notice. Compression libraries remain implementation dependencies,
not HTTP protocol dependencies.

## Values and columns

Supported layouts: Int/UInt 8–256, Float32/64, Bool, String, FixedString, Date,
Date32, DateTime, DateTime64, Decimal/Decimal32/64/128/256, Enum8/16, UUID, IPv4/IPv6,
Nothing, Nullable, Array, named/unnamed Tuple, Map and LowCardinality (including
nullable dictionary values and nested column prefixes).

| Column | `value::data` representation |
|---|---|
| UInt up to 64, Bool, Date, DateTime, IPv4 | `std::uint64_t` |
| Int up to 64, Date32, DateTime64, small Decimal, Enum | `std::int64_t` |
| Float32/64 | `double` |
| String, FixedString | Binary `std::string`; no implicit trimming/padding |
| UUID, IPv6, Int/UInt128/256, Decimal128/256 | Exact Native storage bytes in `std::string` |
| NULL/Nothing | `std::monostate` |
| Array/Tuple | `value::sequence` |
| Map | Sequence of two-element key/value sequences |
| LowCardinality | Materialized values, retaining the original column type |

Decimals remain unscaled integers with scale in the type name. Temporal integers
retain native epoch units. Numeric overflow, wrong variants, unequal row counts,
malformed dictionaries, unsupported types and invalid FixedString lengths fail
explicitly. Dynamic/Variant/JSON/AggregateFunction wire layouts are not inferred or
silently skipped; select a supported projection when querying such server types.

## Client API and ownership

```cpp
auto client::connect(connection_options = {}, cancel_token* = nullptr)
    -> task<std::expected<server_info, error>>;
auto client::query(std::string sql, query_options, cancel_token* = nullptr)
    -> task<std::expected<query_result, error>>;
auto client::query(std::string sql, std::string query_id = {}, cancel_token* = nullptr)
    -> task<std::expected<query_result, error>>;
auto client::query_blocks(std::string sql, block_sink, std::string query_id = {},
    cancel_token* = nullptr) -> task<std::expected<query_result, error>>;
auto client::insert(std::string table, block, std::string query_id = {},
    cancel_token* = nullptr) -> task<std::expected<query_result, error>>;
auto client::cancel_current_query(cancel_token* = nullptr)
    -> task<std::expected<void, error>>;
```

One executor owns a client. Overlapping ordinary operations return `busy` without
interrupting the owner. Native Cancel is admitted only while SELECT is reading
results; retain the client until both Cancel and the query settle. Cancelled queries
discard their transport rather than returning partial results as success.
Precancellation preserves an established connection. After server/protocol/I/O
failure or a sink exception, reconnect explicitly before reuse. Inserts are never
silently retried: an interrupted write may already have been accepted.

`query_result::blocks` includes a zero-row schema block when sent by the server.
Sink references last only until the callback returns. The absolute operation
budget covers transport work; application callbacks must cooperate and bound
their own work. Default connect/operation timeouts are 5/30 seconds.

## Connection pool and Application

`connection_pool_options` selects connection settings and minimum/maximum capacity.
Leases are move-only; healthy connections return to idle storage, failed connections
are discarded. Exhaustion returns `busy` immediately, providing bounded admission
without an unbounded waiter queue. `close()` stops new acquisition, closes idle
connections and returns `operation_in_progress` while leases remain. Keep borrowers
alive until their work finishes, release them and retry shutdown. A stopped pool is
not reopened implicitly.

Application configuration is explicit:

```json
{
  "services": {
    "analytics": {
      "type": "clickhouse",
      "instance": "analytics",
      "enabled": true,
      "required": true,
      "host": "127.0.0.1",
      "port": 9000,
      "database": "default",
      "username": "default",
      "password": "${CLICKHOUSE_PASSWORD}",
      "compression": "lz4",
      "minimum_size": 1,
      "maximum_size": 8,
      "connect_timeout_ms": 5000,
      "operation_timeout_ms": 30000
    }
  }
}
```

Call `enable_auto_configuration()`. Unknown options, invalid types/capacities and
unknown compression modes fail before starting listeners. Optional TLS options
are `tls` and `ca_file`; Native TLS normally uses port 9440. Each event loop gets
an independent pool; total capacity must cover all participating loops. Repository
factories resolve the service during build but select its loop-local pool only when
an operation executes. HTTP readiness reads the common cached health registry.
Required/optional startup, recovery budgets and rollback use the existing lifecycle.
Failed shutdown retains cleanup ownership until outstanding borrowers return.

## ORM and repository integration

```cpp
application::add_repository<analytics_record>(context.components,
    {.instance = "analytics",
     .provider = application::database_provider::clickhouse});
```

Automatic provider selection requires exactly one configured provider with the
requested instance name. ClickHouse uses the same `managed_repository<T>`, model
metadata, interceptors, query/update wrappers, XML statements, pagination and
projection path as the existing databases. The adapter translates `{}` bind slots
outside quoted literals/comments into typed server parameters. INSERT VALUES is
rendered as INSERT SELECT so the Native query path does not confuse inline SQL data
with the separate block-insertion protocol. Native `client::insert` remains the
batch-column transport API.

Updates/deletes render `ALTER TABLE ... UPDATE/DELETE ... SETTINGS mutations_sync=1`.
Mutation success means completion, not a fabricated relational affected-row count.
Ordinary repository writes and batch inserts do not issue synthetic transactions.
The following fail explicitly: transactions, relational atomic upsert,
auto-increment keys and affected-row-based optimistic locking. MergeTree primary
keys are sorting/index keys, not enforced uniqueness.

The result adapter preserves nulls and scalars, exact decimal text, wide integer
text, UUID text and UTC calendar values. Finer-than-microsecond timestamps retain
text instead of being silently rounded. Pass `orm_value_decoder` to `orm_client`
for an explicit mapping of nested/special columns into common SQL values; returning
`nullopt` delegates to the standard conversion.

`relational_dialect::clickhouse` renders typed migrations using MergeTree/ORDER BY,
nullable columns, application-generated keys, defaults, added columns and CHECK
constraints. Generic relational foreign keys, uniqueness, B-tree indexes, identity
columns and implicit JSON type mapping are rejected before any SQL is executed.
The generic decimal migration type maps explicitly to `Decimal(38,9)`; use native
DDL for a different scale/engine.

## Validation

Build and run `test_clickhouse`, `test_clickhouse_compression`,
`test_clickhouse_transport`, `test_clickhouse_live`, `test_application_clickhouse`.
Compression tests use independent official CLI byte fixtures, corrupted checksums,
truncated inputs, invalid sizes and large frames. Loopback fixtures keep exact
packet checks. Real-server tests cover all four transport modes, Native Cancel,
parameter escaping, external tables, LowCardinality, large/multi-frame data, TLS
trust/name rejection, pools, real repository CRUD, pagination, batch inserts,
migrations, unsupported capabilities, restart/retained cleanup and auto-configuration.

```bash
python3 testing/database/run_clickhouse_native.py \
  --clickhouse "$HOME/.local/bin/clickhouse" --test-dir build/native \
  --application-executable build/native/testing/tests/test_application_clickhouse
```

The runner creates temporary loopback-only servers, generated test certificates
and fresh data, then terminates and removes them. CTest registers it when Python,
OpenSSL and a local ClickHouse binary are available. Direct live test executables
use `CNETMOD_CLICKHOUSE_TEST_PORT` and return 77 when no test endpoint is configured.

References: [Native packets](https://clickhouse.com/docs/native-protocol/client),
[official revision gates](https://github.com/ClickHouse/ClickHouse/blob/master/src/Core/ProtocolDefines.h),
[compression modes](https://clickhouse.com/docs/guides/clickhouse/data-modelling/compression/compression-modes).
