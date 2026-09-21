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

An XML mapper is not a second persistence stack. It defines SQL while execution
continues through
`application_repository<T> -> repository<T> -> mapper<T> -> database_session`,
so typed CRUD and XML share leases, transactions, interceptors, diagnostics and
telemetry.

### Supported surface

| Category | Supported behavior |
|---|---|
| Loading | `load_xml`, `load_file`, and non-recursive `load_directory` for direct `.xml` children |
| Top-level elements | `<select>`, `<insert>`, `<update>`, `<delete>`, `<sql>`, `<resultMap>` |
| Dynamic SQL | `<if>`, `<where>`, `<set>`, `<trim>`, `<foreach>`, `<choose>/<when>/<otherwise>`, `<include>`, `<bind>` |
| Parameters | Bound `#{name}`, dotted properties, collections, foreach item/index, and raw `${name}` substitution |
| Expressions | Null/boolean/number/string literals, dotted paths, parentheses, comparison, logical, arithmetic and unary operators |
| Results | Direct `CNETMOD_MODEL` mapping or automatic `resultMap` handling for `<id>`, `<result>`, associations and collections |
| Providers | MySQL and PostgreSQL; PostgreSQL placeholders become `$1...$n` |

Arbitrary projections and infrastructure queries can call
`select_xml_result()`. It returns a `cnetmod::database::query_result` retaining
column metadata, rows, affected rows and database diagnostics. This is a
provider-neutral result rather than a native MySQL/PostgreSQL protocol object,
and it still traverses the normal binding, interceptor, lease and telemetry
pipeline. Migration scripts do not use this entry point; raw migration SQL
continues through `schema_migration_runner`.

`${name}` inserts text directly and must only receive application allow-listed
identifiers such as known sort columns. Never forward request text into it.
`jdbcType/javaType/typeHandler/mode/numericScale` metadata is parsed and
preserved, but the current runtime binds the value only; Java type handlers and
stored-procedure OUT parameters are not executed.

`resultMap` associations and collections support joined object graphs and
`<id>`-based de-duplication. A C++ model explicitly binds those members through
`xml_object_graph_binder<T>`. An `association` or `collection` with
`select="..."` stores nested-select metadata but does not issue hidden queries;
use an explicit coroutine query or `lazy_relation<T>` to avoid implicit I/O and
N+1 behavior.

```cpp
cnetmod::orm::mapper_registry registry;
auto loaded = registry.load_directory("mapper");

cnetmod::orm::param_context parameters;
parameters.set("id", std::int64_t{42});

auto result = co_await accounts->get_one_xml(
    registry, "AccountMapper.findById", parameters);
```

XML selects and writes run through the same Mapper/Repository path as typed CRUD. They share the leased connection, transaction, policies, result mapping, dialect normalization and telemetry. There is no parallel XML repository.

See [the canonical XML mapper reference](../../../skill/database/database-orm.md#xml-mappers)
for exact tag semantics, result mapping rules, parameter types, limitations and
a composed example.

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
