# ORM Guide

cnetmod exposes one provider-neutral ORM path:

```text
application_runtime::repository<T>()
  -> application_repository<T>
    -> repository<T>
      -> mapper<T>
        -> internal database_session
          -> session_gateway
            -> MySQL/PostgreSQL pool
```

Application code uses `application_repository<T>`. `mapper<T>` is the sole model-aware SQL boundary and contains both typed CRUD and MyBatis-style XML statements. A raw `database_session` is an internal execution/transaction context, not an application CRUD facade.

## Imports and model

```cpp
#include <cnetmod/orm.hpp>

import std;
import cnetmod.application;
import cnetmod.orm;

struct account
{
    std::int64_t id{};
    std::string display_name;
};

CNETMOD_MODEL(account, "accounts",
    CNETMOD_FIELD(id, "id", bigint, PK | AUTO_INC),
    CNETMOD_FIELD(display_name, "display_name", varchar))
```

## Resolve the repository

```cpp
auto accounts = host->runtime().repository<account>("primary");
if (!accounts)
    return EXIT_FAILURE;
```

If MySQL and PostgreSQL share the same instance name, pass `database_provider::mysql` or `database_provider::postgresql` explicitly.

## CRUD and wrappers

```cpp
cnetmod::orm::query_wrapper<account> query;
query.eq(&account::display_name, "Ada")
    .order_by_desc(&account::id)
    .limit(20);

auto listed = co_await accounts->list(query);
auto one = co_await accounts->get_one(query);
auto page = co_await accounts->page(1, 20, query);

account value{.display_name = "Grace"};
auto saved = co_await accounts->save(value);
```

`get_one()` has strict cardinality: zero rows is an empty successful result, one row succeeds, and multiple rows return `std::errc::result_out_of_range`. Check `model_result<T>::ok()` before inspecting data. Native error code, SQLSTATE, framework error, failed operation and batch item indices are preserved.

The repository also provides projections, streaming, batch writes, native upsert and guarded update/delete. An unbounded update/delete requires the explicit `allow_full_table` token.

## XML statements

```cpp
cnetmod::orm::mapper_registry registry;
auto loaded = registry.load_directory("mapper");

cnetmod::orm::param_context parameters;
parameters.set("id", std::int64_t{42});

auto result = co_await accounts->get_one_xml(
    registry, "AccountMapper.findById", parameters);
```

XML selects and writes run through the same Mapper/Repository path as typed CRUD. They share the leased connection, transaction, policies, result mapping, dialect normalization and telemetry. There is no parallel XML repository.

## Cross-model transaction

```cpp
auto result = co_await accounts->transaction<std::int64_t>(
    [&](auto& unit)
        -> cnetmod::task<std::expected<std::int64_t, std::string>>
    {
        auto account_mapper = unit.template mapper<account>();
        auto audit_mapper = unit.template mapper<audit_record>();

        auto inserted = co_await account_mapper.insert(value);
        if (inserted.is_err())
            co_return std::unexpected(inserted.error_msg);

        auto audit = co_await audit_mapper.execute_xml(
            registry, "AuditMapper.append", parameters);
        if (audit.is_err())
            co_return std::unexpected(audit.error_msg);

        co_return 1;
    });
```

Every Mapper obtained from the unit shares one connection and one transaction. Do not retain it after the callback.

## Current API reference

The complete, source-checked method inventory and configuration semantics are maintained in [`skill/database/database-orm.md`](../../../skill/database/database-orm.md). Run `python tools/check_orm_docs.py` after ORM interface changes.
