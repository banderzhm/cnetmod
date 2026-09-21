# MySQL

cnetmod provides a coroutine-native MySQL wire client, prepared statements, connection pooling, TLS, health checking and an Application-managed ORM adapter.

## Modules

```cpp
import std;
import cnetmod.protocol.mysql; // protocol client and pool
import cnetmod.application;    // managed service and repository
import cnetmod.orm;            // model, wrappers and result contracts
```

Protocol code uses `cnetmod::mysql`. Provider-neutral persistence code uses `cnetmod::orm`; there is no MySQL-specific business Mapper or Repository API.

## Application ORM

```cpp
auto users = host->runtime().repository<user_record>("primary", {},
    cnetmod::application::database_provider::mysql);
if (!users)
    co_return;

auto result = co_await users->get_by_id(
    cnetmod::orm::param_value::from_int(42));
```

The managed MySQL adapter supplies the gateway, result adapter, wire cursor strategy and native upsert syntax. Typed CRUD and XML statements still execute through the common `repository<T> -> mapper<T>` path.

## Operational rules

- Lease a client from the pool; do not multiplex concurrent operations on one protocol client.
- Use prepared/bound parameters instead of string interpolation.
- Keep a transaction on one lease until commit or rollback.
- Discard a connection after incomplete protocol exchange or transport failure.
- Bound connect, checkout, query and shutdown time.
- Use TLS verification and least-privilege credentials in production.
- Do not record SQL parameters or credentials in telemetry.

See the current [ORM guide](../advanced/orm-guide.md) and [transaction guide](../mysql_transaction.md).
