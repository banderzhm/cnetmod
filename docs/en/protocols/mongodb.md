# MongoDB

cnetmod provides a coroutine-native MongoDB connection with OP_MSG framing,
BSON documents, hello negotiation, authentication, TLS configuration, command
correlation, and bounded message validation.

## Connection and command API

```cpp
import cnetmod.protocol.mongodb;

mongodb::connection_options options;
options.host = config.host;
options.port = config.port;
options.database = config.database;
options.username = config.username;
options.password = config.password;
options.authentication_database = config.authentication_database;
options.connect_timeout = std::chrono::seconds{10};
options.tls = true;
options.tls_verify = true;
options.tls_ca_file = config.ca_file;

mongodb::connection connection(context);
auto connected = co_await connection.connect(options);
if (!connected) {
    logger::error("MongoDB connection failed: {}", connected.error().message);
    co_return;
}

auto reply = co_await connection.command(
    options.database, mongodb::bson_document{{"ping", std::int32_t{1}}});
```

A connection carries at most one in-flight command. High-concurrency services
must use a bounded set of independent connections and apply backpressure while
all leases are busy. Do not share one connection concurrently between request
handlers.

## BSON ownership

`bson_document`, `bson_array`, `bson_value`, and `bson_binary` own their values.
Validate application-level document sizes before allocating large buffers. The
wire layer also enforces configured message limits and the server limits learned
during hello. Do not retain views into temporary encoded buffers.

MongoDB commands return a BSON reply even when the server reports a command
error. Check the result wrapper and the reply's `ok`, `code`, `codeName`, and
`errmsg` fields. Keep error messages out of user-facing responses unless they
are sanitized.

## Production topology and high availability

`mongodb::connection` represents one physical connection. Production services
use `connection_pool` or `topology_connection_pool`. The topology layer
implements replica-set discovery, SDAM monitoring, read preference, server
selection, and primary reselection. The pool provides FIFO suspended waiters,
timeouts, targeted cancellation, close wake-up, stale-connection eviction, and
idle maintenance.

The driver also provides retryable reads/writes, logical sessions, transaction
pinning, unknown-commit-result retry, Change Stream resume tokens and automatic
resume, plus OP_COMPRESSED zlib/noop negotiation and complete BSON wire types.
Replay still has to satisfy MongoDB labels, transaction rules, and application
idempotency.

## Migrating a Spring Boot service

`example_mongodb_production_service` separates configuration, Repository,
business Service, and application lifecycle. These map to Spring Boot's
`@ConfigurationProperties`, Repository, `@Service`, health indicators, and
`SmartLifecycle`. Replica-set seeds, pool bounds, wait/command timeouts, read
preference, retry, and TLS are injected through configuration. Shutdown stops
admission, drains in-flight commands and Change Streams, then closes the
topology pool.

## Production checklist

- Use SCRAM-SHA-256 and a least-privilege database user.
- Require TLS certificate verification outside trusted local development.
- Prefer private database ports in production. When public interoperability is
  required, enforce authentication, least privilege, TLS, and source controls.
- Bound connection checkout, connect, command, response size, and shutdown.
- Correlate every response to its request ID and discard a connection after a
  framing, correlation, or transport error.
- Apply pool backpressure and expose checkout latency, active/idle counts,
  command latency, failure labels, and reconnect counts.
- Retry only when both the MongoDB error labels and application idempotency make
  replay safe.
- Stop admission and drain outstanding commands before closing connections.
- Never log credentials, complete connection URIs, or command documents that
  may contain personal data.

## Interoperability testing

`testing/database/mongodb` compares the native client with pymongo against the
same live service. Supply credentials using `CNETMOD_MONGODB_URI` and the native
executable using `CNETMOD_MONGODB_DRIVER`. See `testing/database/README.md`.
