# PostgreSQL

cnetmod provides a coroutine-native PostgreSQL client, prepared statements, connection pooling, TLS and an Application-managed ORM adapter.

## Modules

```cpp
import std;
import cnetmod.protocol.postgresql;
import cnetmod.application;
import cnetmod.orm;
```

Wire objects live in `cnetmod::postgresql`. Models, wrappers, Mapper and Repository contracts live in `cnetmod::orm`. PostgreSQL does not expose a second model CRUD facade.

## Application ORM

```cpp
auto orders = host->runtime().repository<order_record>("primary", {},
    cnetmod::application::database_provider::postgresql);
if (!orders)
    co_return;

auto page = co_await orders->page(1, 50);
```

The PostgreSQL adapter supplies its gateway, result mapping, `$n` placeholder normalization and `ON CONFLICT` upsert generation. Typed CRUD and XML statements share the common Mapper/Repository transaction and policy chain.

## Operational rules

- One physical client is not an implicit multiplexing boundary.
- Retain one pool lease from `BEGIN` through `COMMIT` or `ROLLBACK`.
- Return a client only after `ReadyForQuery`; evict protocol/transport failures.
- Use bound parameters, verified TLS, least-privilege credentials and bounded timeouts.
- Retry only known-idempotent operations; never replay an ambiguous commit.

See the current [ORM guide](../advanced/orm-guide.md).
