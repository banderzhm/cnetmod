# PostgreSQL

cnetmod provides a coroutine-native PostgreSQL client. MySQL and PostgreSQL
integrate independently with the protocol-neutral `cnetmod::orm`; neither
protocol depends on the other. Applications can move a `CNETMOD_MODEL` entity
and its CRUD service to PostgreSQL without maintaining duplicate entities.

## Modules and files

Import the facade in application code:

```cpp
import cnetmod.protocol.postgresql;
```

The implementation uses named interface/implementation units such as
`postgresql_connection.cppm` and `postgresql_connection.cpp`. It deliberately
does not introduce catch-all filenames such as `types.cppm`.

The facade exports connection options, result rows, prepared statements, the
connection client, and the PostgreSQL ORM adapter.

## Connecting safely

Construct one `postgresql::client` per physical server connection. A client is
not an implicit multiplexing boundary: do not run concurrent operations on the
same client. A service should lease independent clients from its connection
pool, perform one transaction at a time, and return the lease only after the
transaction reaches `ReadyForQuery`.

Configuration belongs in environment variables or a secret manager. Production
defaults should require certificate verification, set a connect timeout, and
use a dedicated least-privilege role. Never embed a URI or password in source.

```cpp
postgresql::connection_options options;
options.host = config.host;
options.port = config.port;
options.username = config.username;
options.password = config.password;
options.database = config.database;
options.connect_timeout = std::chrono::seconds{10};
options.tls = postgresql::tls_mode::verify_full;
options.tls_ca_file = config.ca_file;

postgresql::client connection(context);
auto connected = co_await connection.connect(options);
if (connected.is_err()) {
    logger::error("PostgreSQL connection failed: {}", connected.error_msg);
    co_return;
}
```

Use `server_parameters()`, `backend_process_id()`, and `secure_channel()` for
diagnostics and health metadata. Do not log credentials or a full connection
URI.

## Shared ORM model API

PostgreSQL exports `cnetmod::orm::postgresql_session`. ORM-facing APIs live in
`cnetmod::orm`; the `cnetmod::postgresql` namespace is reserved for the wire
client, connection pool, protocol metadata, and dialect adapter. Entity
metadata, `CNETMOD_MODEL`, result mapping, UUID, and Snowflake key strategies
are database independent.

```cpp
struct Order {
    std::int64_t id{};
    std::string customer;
    std::int64_t total_cents{};
};

CNETMOD_MODEL(Order, "orders",
    CNETMOD_FIELD(id, "id", bigint, PK | AUTO_INC),
    CNETMOD_FIELD(customer, "customer", varchar),
    CNETMOD_FIELD(total_cents, "total_cents", bigint))

cnetmod::orm::postgresql_session database(connection);
auto orders = co_await database.find_all<Order>();
```

Keep portable query-builder expressions free of dialect-specific quoting and
functions. Raw SQL remains database-specific. Schema migrations must be
reviewed when moving from MySQL because auto-increment syntax, JSON operators,
upserts, boolean coercion, and identifier folding differ.

## Migrating a Spring Boot service

`example_postgresql_production_service` is split into configuration binding,
Repository, business Service, application lifecycle, pool, and health
responsibilities. Environment variables replace `application.yml` and Secret
injection; request handlers call the Service instead of owning connections.
Transactions retain one pool lease, saturation applies backpressure, transient
connect failures use bounded retry, and shutdown stops admission before it
drains work and closes the I/O context.

## Prepared statements and transactions

Use prepared statements for repeated parameterized work. Parameters remain
separate from SQL and are never interpolated into the query string.

```cpp
auto statement = co_await connection.prepare(
    "SELECT id, customer, total_cents FROM orders WHERE id = $1");
if (statement.is_err())
    co_return;

std::array parameters{postgresql::param_value::from_int(order_id)};
auto rows = co_await connection.execute(*statement, parameters);
```

Transaction-owning code must retain the same connection from `BEGIN` through
`COMMIT` or `ROLLBACK`. If cancellation or a database error leaves the session
in a failed transaction, roll it back before returning it to a pool. Discard a
connection after a transport or protocol failure.

## Production checklist

- Require TLS and peer verification outside trusted local development.
- Bound connect, checkout, query, and shutdown time; propagate cancellation.
- Size pools from database capacity, not request concurrency. Apply backpressure
  when all leases are busy.
- Use prepared parameters and least-privilege roles.
- Roll back dirty leases, periodically validate idle connections, and evict
  protocol/transport failures.
- Record latency, pool wait time, SQLSTATE, retry count, and saturation without
  logging SQL parameters that may contain personal data.
- Retry only operations known to be idempotent, with bounded exponential
  backoff and jitter. Never blindly replay an ambiguous commit.
- Shut down by stopping admission, draining work, terminating idle clients, and
  then stopping the I/O context.

## Interoperability testing

`testing/database/postgresql` compares the native client with psycopg against
the same live PostgreSQL server. Credentials are supplied only through
`CNETMOD_POSTGRESQL_URI`; the native executable path is supplied through
`CNETMOD_POSTGRESQL_DRIVER`. See `testing/database/README.md` for the exact
commands.
