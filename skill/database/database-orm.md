# Database ORM

> Provider-neutral C++23 ORM with a MyBatis-Plus-style Mapper/Repository API and MyBatis-style XML statements.

**Primary import**: `import cnetmod.orm;`
**Application import**: `import cnetmod.application;`

## Architecture

```text
application_runtime::repository<T>()
  -> application_repository<T>
    -> repository<T>
      -> mapper<T>                 typed CRUD and XML statements
        -> database_session        raw internal execution/transaction context
          -> session_gateway       lease and unit-of-work ownership
            -> MySQL/PostgreSQL pool
```

The public boundary is deliberate:

- Application code resolves `application_repository<T>` from `application_runtime`.
- Infrastructure tests and custom gateways may construct `repository<T>` or `mapper<T>`.
- `mapper<T>` is the only public model-aware SQL boundary.
- `database_session` exposes raw parameterized execution and transaction control only. It does not expose model CRUD.
- MySQL and PostgreSQL differences stay behind gateways, result adapters, cursor strategies, dialects and native upsert generation.
- XML is an extension of the same Mapper/Repository path, not a second persistence facade.

## Model declaration

```cpp
#include <cnetmod/orm.hpp>

import std;
import cnetmod.orm;

struct user_record
{
    std::int64_t id{};
    std::string name;
    std::optional<cnetmod::orm::calendar_datetime> deleted_at;
};

CNETMOD_MODEL(user_record, "users",
    CNETMOD_FIELD(id, "id", bigint, PK | AUTO_INC),
    CNETMOD_FIELD(name, "name", varchar),
    CNETMOD_FIELD(deleted_at, "deleted_at", datetime,
        NULLABLE | LOGIC_DELETE))
```

Model fields support scalar values, enums, UUIDs, calendar date/time values and documented optional mappings. Use `calendar_datetime` for SQL `DATETIME` instead of duplicating Unix/wall-clock conversion helpers in application code.

## Application entry point

Resolve a repository after the application host has been built and its managed services have been registered:

```cpp
auto users = host->runtime().repository<user_record>("primary");
if (!users)
    return EXIT_FAILURE;

auto result = co_await users->get_by_id(
    cnetmod::orm::param_value::from_int(42));
if (result.is_err())
    co_return;

auto user = result.first();
```

When a MySQL and PostgreSQL service deliberately use the same instance name, specify the provider:

```cpp
auto users = host->runtime().repository<user_record>("primary", {},
    cnetmod::application::database_provider::postgresql);
```

Automatic provider selection succeeds only when exactly one supported provider owns the requested instance.

## Repository API

`application_repository<T>` is the application-facing API. The following inventory is checked against `src/application/orm_repository.cppm` by `tools/check_orm_docs.py`.

<!-- ORM_REPOSITORY_API_BEGIN -->
| Method | Purpose |
|---|---|
| `get_by_id` | Read one model by primary key. |
| `get_one` | Require zero or one matching model; multiple rows are an error. |
| `list` | Read matching models. |
| `list_by_ids` | Read models by primary keys. |
| `list_by_map` | Build equality predicates from named values. |
| `exists` | Test whether a matching row exists. |
| `count` | Count matching rows. |
| `page` | Return a typed page and total count. |
| `select_maps` | Return projected rows keyed by column name. |
| `select_objects` | Return the first projected column. |
| `page_maps` | Return a projected page and total count. |
| `select_xml` | Execute a typed XML select. |
| `select_xml_result` | Execute an XML select and return the provider-neutral column/row result. |
| `select_xml_as` | Execute an XML select as a strongly typed read-only projection. |
| `get_one_xml` | Execute a cardinality-checked XML select. |
| `save` | Insert one model. |
| `update_by_id` | Update one model by primary key. |
| `update` | Apply an `update_wrapper<T>`. |
| `save_or_update` | Choose insert/update from model identity. |
| `upsert` | Use the provider-native atomic upsert. |
| `remove_by_id` | Delete one row by primary key. |
| `remove` | Delete rows selected by a wrapper. |
| `remove_by_ids` | Delete rows by primary keys. |
| `remove_by_map` | Delete rows selected by named equality predicates. |
| `execute_xml` | Execute an XML insert, update or delete. |
| `save_batch` | Insert models in bounded batches. |
| `update_batch_by_id` | Update models in bounded batches. |
| `save_or_update_batch` | Apply identity-based save/update in batches. |
| `upsert_batch` | Apply provider-native upsert in batches. |
| `for_each` | Stream typed models with backpressure. |
| `for_each_map` | Stream projected rows with backpressure. |
| `transaction` | Execute a cross-model unit of work on one connection. |
<!-- ORM_REPOSITORY_API_END -->

`mapper<T>` is the infrastructure-level BaseMapper equivalent. Its inventory is checked against `src/database/orm/mapper.cppm` by the same validation script.

<!-- ORM_MAPPER_API_BEGIN -->
| Method | Purpose |
|---|---|
| `configure` | Install automatic model policies on the current session. |
| `prepare_select` | Build the intercepted statement used by provider cursors. |
| `open_cursor` | Open a session-bound cursor. |
| `select_by_id` | Select by primary key. |
| `select_one` | Strict single-row select. |
| `select_first` | Explicitly select the first matching row. |
| `select_list` | Select matching models. |
| `select_by_ids` | Select by primary keys. |
| `select_by_map` | Select by named equality predicates. |
| `exists` | Test for a matching row. |
| `count` | Count matching rows. |
| `select_page` | Select a typed page. |
| `select_maps` | Select projected rows. |
| `select_objects` | Select the first projected column. |
| `select_maps_page` | Select a projected page. |
| `select_xml` | Execute a typed XML select. |
| `select_xml_result` | Execute an XML select without applying a model projection. |
| `select_xml_as` | Execute an XML select as a strongly typed read-only projection. |
| `select_one_xml` | Execute a cardinality-checked XML select. |
| `execute_xml` | Execute an XML write. |
| `insert` | Insert one model. |
| `update_by_id` | Update one model by primary key. |
| `update` | Apply an update wrapper. |
| `save_or_update` | Choose insert/update from identity. |
| `upsert` | Use provider-native atomic upsert. |
| `remove` | Delete rows selected by a wrapper. |
| `remove_by_id` | Delete by primary key. |
| `remove_by_ids` | Delete by primary keys. |
| `remove_by_map` | Delete by named equality predicates. |
| `insert_batch` | Insert a bounded batch. |
| `update_batch` | Update a bounded batch. |
| `save_or_update_batch` | Apply identity-based save/update to a batch. |
| `upsert_batch` | Apply provider-native upsert to a batch. |
| `for_each` | Stream typed models. |
| `for_each_map` | Stream projected rows. |
<!-- ORM_MAPPER_API_END -->

All database outcomes use `model_result<T>` or `page_result<T>`. Do not infer success from an empty vector:

```cpp
auto page = co_await users->page(1, 50, query);
if (!page.ok())
{
    // page.records contains framework_error, native error_code,
    // sql_state, error_msg and operation.
    co_return;
}
```

## Query and update wrappers

自动逻辑删除默认使用 `deleted = 0/1`。同一应用存在可空 `deleted_at`
模型时，构建该模型仓储要在 `automatic_interceptor_options.logical_delete_policy`
中传入 `field_name = "deleted_at"`、`mode = nullable_datetime`，并按需指定
`touch_fields`；策略在拦截链创建时固定，不会改动其他模型。复杂 JOIN/XML
查询仍须核对 SQL 中的别名与删除条件，不能依赖字符串注入代替 SQL 语义。
`nullable_datetime` 的自动 DELETE 转 UPDATE 当前使用数据库 `CURRENT_TIMESTAMP`；
若列约定存 UTC 墙钟且会话时区不保证 UTC，应用应像 Nexus 一样用带 UTC
`calendar_datetime` 参数的选择性更新完成软删除。

`FILL_INSERT` 与 `FILL_INSERT_UPDATE` 直接从模型元数据读取；普通仓储请求
不会反复修改全局注册表。自定义填充配置可按表名和字段名在启动期注册。普通 ORM
实体写入自动填充，同一次写入的多个时间戳字段使用同一个时间值。插入时只填
默认空值，不覆盖调用方显式给出的历史时间；`FILL_INSERT` 字段不会出现在
按实体更新或原生 upsert 的 UPDATE 列清单中。可变实体更新会回填实体；const
实体与批量 const 更新会填充副本，不修改调用方对象。`update_wrapper` 会自动补上
`FILL_INSERT_UPDATE` 字段，但显式 `.set()` 的时间值优先（适合一次业务操作
必须写入多列相同时间）。XML 写入不参与实体自动填充，仍须显式绑定时间参数。

在模型里给 `created_at` 标 `FILL_INSERT`，给 `updated_at` 标
`FILL_INSERT_UPDATE`；字段类型用 `calendar_datetime`。`save()` / `insert()`
的空时间由 ORM 填入 UTC 墙钟时间，导入数据时预先设置的非空时间会保留。
`update_by_id()`、`upsert()` 和 `update_wrapper` 都遵守上述规则；如果一次
更新还要同步写 `deleted_at`、`last_message_at` 等业务时间，先生成一个
`calendar_datetime`，通过 `.set()` 将同一值传给相关列，不依赖分别生成的
时间戳。XML 的 `INSERT`/`UPDATE`/定制 upsert 必须自行绑定该时间值；
自动填充不会解析或改写 XML SQL。真实 MySQL 回归见
`testing/tests/test_orm_tenant_live.cpp` 的
`mysql_live_system_chart_chat_timestamp_fill`。

`query_wrapper<T>` accepts column names and type-safe member pointers. Prefer member pointers in application code:

```cpp
cnetmod::orm::query_wrapper<user_record> query;
query.eq(&user_record::name, "Ada")
    .is_null(&user_record::deleted_at)
    .order_by_desc(&user_record::id)
    .limit(20);

auto rows = co_await users->list(query);
```

It supports comparisons, `LIKE`, `IN`, ranges, null/boolean predicates, nested groups, structured subqueries, joins, projection, grouping, aggregates, ordering and limits. Values are always bound parameters; arbitrary trailing SQL is not part of the API.

Updates use `update_wrapper<T>`:

```cpp
cnetmod::orm::update_wrapper<user_record> update;
update.set(&user_record::name, "Grace")
    .eq(&user_record::id, 42);

auto changed = co_await users->update(update);
```

Unbounded update/delete calls are rejected. A deliberate full-table operation must pass `cnetmod::orm::allow_full_table` explicitly.

## Single-row semantics

- `get_one(query)` and `get_one_xml(...)` default to strict cardinality.
- Zero rows produce a successful empty `model_result<T>`.
- One row produces one value.
- More than one row produces `std::errc::result_out_of_range`.
- XML callers may explicitly pass `single_result_policy::first` when taking the first row is intentional.

## Transactions and cross-model work

Repository writes own their local transaction. A workflow touching multiple models uses `transaction<Result>()` so every Mapper shares the same leased connection and transaction:

```cpp
auto outcome = co_await users->transaction<std::int64_t>(
    [&](auto& unit)
        -> cnetmod::task<std::expected<std::int64_t, std::string>>
    {
        auto user_mapper = unit.template mapper<user_record>();
        auto order_mapper = unit.template mapper<order_record>();

        auto saved = co_await user_mapper.insert(user);
        if (saved.is_err())
            co_return std::unexpected(saved.error_msg);

        auto orders = co_await order_mapper.select_list(order_query);
        if (orders.is_err())
            co_return std::unexpected(orders.error_msg);

        co_return static_cast<std::int64_t>(orders.data.size());
    });
```

The gateway commits only a successful `expected` result. Errors and exceptions roll back. Do not nest repository transactions or retain a Mapper outside the callback.

## XML mappers

XML is a statement-definition layer on the normal Mapper/Repository pipeline. It
does not create a second repository type and it does not own a connection. Load
mapper files once during application composition, keep the registry alive, and
execute statements through `application_repository<T>` or a transaction Mapper.

### Registry and statement lookup

`mapper_registry` supports the following sources and lookups:

| API | Supported behavior |
|---|---|
| `load_xml(text)` | Load one `<mapper>` document from memory. |
| `load_file(path)` | Load one mapper file. |
| `load_directory(path)` | Load every direct child whose extension is `.xml`; it is not recursive. |
| `find_statement(id)` | Resolve a statement ID. Use `namespace.id` in application code to avoid collisions. |
| `statement_type(id)` | Return `select`, `insert`, `update` or `delete`. |
| `statement_parameter_type(id)` | Return the declared metadata string. |
| `statement_result_type(id)` | Return the declared metadata string. |
| `statement_result_map(id)` | Return the declared `resultMap` reference. |

The root must be `<mapper namespace="...">`. Recognized top-level elements are
`<select>`, `<insert>`, `<update>`, `<delete>`, `<sql>` and `<resultMap>`. Every
recognized element needs an `id`. A `<select>` cannot declare both `resultType`
and `resultMap`. `parameterType` and `resultType` are descriptive metadata; C++
parameter and result types remain determined by `param_context` and `mapper<T>`.

### Repository and Mapper execution APIs

| Layer | Select many | Select zero/one | Write |
|---|---|---|---|
| `application_repository<T>` / `repository<T>` | `select_xml` | `get_one_xml` | `execute_xml` |
| transaction `mapper<T>` | `select_xml` | `select_one_xml` | `execute_xml` |

For a strongly typed DTO projection, declare its read-only metadata and call
`select_xml_as<Projection>()`:

```cpp
struct user_summary
{
    std::int64_t id{};
    std::string display_name;
};

CNETMOD_PROJECTION(user_summary,
    CNETMOD_FIELD(id, "id", bigint),
    CNETMOD_FIELD(display_name, "display_name", varchar))

auto summaries = co_await users->select_xml_as<user_summary>(
    registry, "UserMapper.summaries", parameters);
```

`CNETMOD_PROJECTION` deliberately has no table name or persistence identity.
It can receive direct columns or a declared `resultMap`, but it cannot be used
with insert, update or delete CRUD APIs. The repository entity `T` continues
to determine tenant, logical-delete and other automatic policies.

For a truly dynamic projection whose columns are not known at compile time,
both layers expose `select_xml_result()`. It returns
`cnetmod::database::query_result`, including column metadata, rows, affected
rows and native diagnostics. Despite serving an untyped projection, it still
uses the same dynamic SQL builder, bound parameters, dialect normalization,
interceptors, connection lease and telemetry as `select_xml()`.

Do not use `select_xml_result()` as a migration-script escape hatch. Schema
migrations remain infrastructure operations executed by
`schema_migration_runner`; they are not model or XML mapper statements.

`get_one_xml` and `select_one_xml` default to
`single_result_policy::require_unique`: zero rows succeed with empty data, one
row succeeds, and multiple rows report `std::errc::result_out_of_range`. Pass
`single_result_policy::first` only when taking the first row is intentional.

Repository writes execute in the same managed transaction path as typed writes.
Inside `repository.transaction()`, XML and typed operations share the same
leased connection and transaction. Do not retain the transaction Mapper after
the callback.

### Parameters and placeholders

Build parameters with `param_context::set()`, `from_map()`, `from_model()`,
`add_nested()` and `add_collection()`.

| XML form | Behavior |
|---|---|
| `#{name}` | Adds a bound parameter. Values are never inserted into SQL text. |
| `#{name,jdbcType=...,javaType=...,typeHandler=...,mode=...,numericScale=...}` | Parses and preserves mapping metadata. The current protocol execution binds the value; custom `typeHandler` execution and OUT parameters are not implemented. |
| `#{request.id}` | Reads a dotted property from a nested `param_context`. |
| `${name}` | Inserts raw text. Restrict it to values selected from an application-owned allow-list, such as known sort columns; never pass request text directly. |

`param_context::set()` accepts values supported by `to_query_parameter`,
including null, signed/unsigned integers, floating point, strings, blobs,
calendar date/time values and optional values. A missing property resolves to
null. Model overloads use `CNETMOD_MODEL` metadata to expose model fields.

### Dynamic SQL elements

| Element | Supported semantics |
|---|---|
| `<if test="...">` | Render children when the expression is truthy. |
| `<where>` | Render `WHERE` only for non-empty content and remove one leading `AND` or `OR`. |
| `<set>` | Render `SET` only for non-empty content and remove trailing commas. |
| `<trim prefix="" suffix="" prefixOverrides="" suffixOverrides="">` | Add prefix/suffix and remove pipe-delimited boundary tokens. |
| `<foreach collection="" item="" index="" open="" close="" separator="">` | Iterate a `param_context` collection; supports nested item properties and a zero-based index. Empty collections render nothing. |
| `<choose>` / `<when test="...">` / `<otherwise>` | Render the first matching branch, otherwise the fallback. |
| `<sql id="...">` / `<include refid="...">` | Reuse a fragment from the same mapper namespace. |
| `<bind name="..." value="...">` | Evaluate an expression and expose its result to following nodes in the statement. |

Expression tests support `null`, booleans, integer/double/string literals,
dotted properties, parentheses, unary `not`/`!`/minus, comparisons
`== != < > <= >=`, logical `and`/`or` (and `&&`/`||`), and arithmetic
`+ - * / %`. They do not execute arbitrary C++ functions or methods.

### Result mapping

There are two supported result paths:

1. With no `resultMap`, `select_xml()` maps result columns directly into `T`
   using `CNETMOD_MODEL`; `select_xml_as<P>()` uses `CNETMOD_PROJECTION` (or a
   second model). SQL aliases must match a declared field or column name.
2. With `resultMap="MapId"`, execution automatically applies `<id>`, `<result>`,
   nested `<association resultMap="...">` and
   `<collection resultMap="...">`, then projects the object graph into the
   requested model or projection type.

Scalar properties are assigned through normal model setters. For associations
and collections, specialize `xml_object_graph_binder<T>` and use
`mapped_association_as<U>()` / `mapped_collection_as<U>()`; this is explicit
because XML property strings cannot safely identify C++ member offsets.
Joined-row object graphs are de-duplicated by `<id>` mappings. Nullable joined
associations become `std::optional`. `autoMapping="true"` maps otherwise
unmapped columns by snake_case-to-camelCase property name.

Nested-query metadata such as `<association select="...">` and
`<collection select="...">` is parsed, but it is not executed automatically.
Use an explicit coroutine `lazy_relation<T>` loader or issue the secondary XML
statement in application code. This avoids hidden I/O and N+1 queries during
property access. Constructor mappings, discriminators, cache declarations,
stored-procedure OUT parameters and Java `typeHandler` execution are not part
of the current XML runtime.

### Complete example

Load XML into `mapper_registry`, then call XML statements through the same repository:

```cpp
cnetmod::orm::mapper_registry registry;
auto loaded = registry.load_directory("mapper");
if (!loaded)
    co_return;

cnetmod::orm::param_context parameters;
parameters.set("id", std::int64_t{42});

auto selected = co_await users->get_one_xml(
    registry, "UserMapper.findById", parameters);

auto summaries = co_await users->select_xml_as<user_summary>(
    registry, "UserMapper.summaries", parameters);
```

Example XML:

```xml
<mapper namespace="UserMapper">
  <sql id="userColumns">
    u.id AS user_id, u.name AS display_name, r.id AS role_id, r.name AS role_name
  </sql>

  <resultMap id="UserMap" type="user_record" autoMapping="false">
    <id property="id" column="user_id"/>
    <result property="name" column="display_name"/>
    <collection property="roles" resultMap="RoleMap"/>
  </resultMap>

  <resultMap id="RoleMap" type="role_record" autoMapping="false">
    <id property="id" column="role_id"/>
    <result property="name" column="role_name"/>
  </resultMap>

  <select id="find" parameterType="map" resultMap="UserMap">
    SELECT <include refid="userColumns"/>
    FROM users u
    LEFT JOIN user_roles ur ON ur.user_id = u.id
    LEFT JOIN roles r ON r.id = ur.role_id
    <where>
      <if test="id != null">AND u.id = #{id,jdbcType=BIGINT}</if>
      <if test="name != null and name != ''">AND u.name = #{name}</if>
      <if test="statuses != null">
        AND u.status IN
        <foreach collection="statuses" item="status"
                 open="(" close=")" separator=",">
          #{status.value}
        </foreach>
      </if>
    </where>
  </select>

  <select id="summaries" parameterType="map" resultType="user_summary">
    SELECT u.id, u.name AS display_name
    FROM users u
    <where>
      <if test="id != null">AND u.id = #{id}</if>
    </where>
  </select>

  <update id="rename" parameterType="map">
    UPDATE users
    <set>
      <if test="name != null">name = #{name},</if>
      updated_at = #{updatedAt}
    </set>
    WHERE id = #{id}
  </update>
</mapper>
```

`select_xml`, `select_xml_as`, `get_one_xml` and `execute_xml` reuse the same leased connection,
transaction, SQL dialect, placeholder normalization, model mapping,
diagnostics, automatic policies and instrumentation. MySQL uses its parameter
adapter; PostgreSQL placeholders are normalized to `$1`, `$2`, and so on.

Inside `transaction`, obtain a normal Mapper and call `select_xml`, `select_one_xml` or `execute_xml` on it. There is no separate XML unit-of-work object.

## Streaming and cursors

Application code normally uses `for_each` or `for_each_map`:

```cpp
cnetmod::orm::stream_options options{
    .batch_size = 128,
    .max_rows = 10'000,
};

auto streamed = co_await users->for_each(query,
    [](const user_record& row)
        -> cnetmod::task<std::expected<void, std::string>>
    {
        co_return {};
    }, options, cancellation);
```

The handler is awaited before the next batch, providing backpressure. Options bound batch size, total rows and deadline. MySQL repositories use the provider cursor strategy; other providers use the session strategy. `mapper<T>::open_cursor()` is an infrastructure-level API tied to the Mapper session lifetime, not an Application API.

## Automatic persistence policies

Pass `automatic_interceptor_options` when resolving the repository. The configured chain applies consistently to typed CRUD and XML statements:

- tenant predicates;
- logical delete, including structured `touch_fields`;
- automatic field fill;
- optimistic locking;
- full-table update/delete protection;
- query observation and cache policies where configured.

The framework does not accept arbitrary SQL expressions in logical-delete touch fields.

Per-request row visibility is an automatic interceptor policy. Mark the model
partition and owner columns with `DATA_PARTITION` and `DATA_OWNER`, bind one
`data_permission_scope` in the HTTP request scope during authentication, then
resolve the repository with `runtime.repository<T>(request, "primary")`.
Typed CRUD, XML statements, projections and transactions all pass through the
same frozen chain. Service and Mapper code do not receive, resolve or bind a
data-scope object. The request repository owns a scope snapshot instead of
using thread-local state, so coroutine suspension cannot leak another user's
policy. A restricted scope with no visible partitions or owner denies every
row.

### SaaS tenant and organization hierarchies

SaaS mode is explicit. Call `runtime.require_tenant_scope(true)` before
serving requests, mark exactly one model column `TENANT_ID`, and bind a
`tenant_scope` to each authenticated request. The runtime copies it into the
repository's frozen policy chain. Missing tenant scope rejects operations on
tenant models; it never means “all tenants.” Leave the runtime switch off for
a single-tenant application. The legacy `tenant_guard` is thread-local and
must not be used as a SaaS request policy.
In strict SaaS mode, models marked `DATA_PARTITION` or `DATA_OWNER` must also
declare a tenant column; otherwise the ORM rejects them instead of applying
only one half of the isolation policy.
Create tenant columns as non-nullable database columns and index the tenant
and partition keys used by filtered queries. Pre-authentication lookups must
first resolve a tenant from a trusted host or tenant identifier and bind a
narrow scope; the ORM cannot infer a tenant from an unauthenticated user ID.

```cpp
runtime.require_tenant_scope(true);
request.scope().bind(std::make_shared<cnetmod::orm::tenant_scope>(
    cnetmod::orm::tenant_scope{
        .tenant_id = current_tenant,
        .readable_tenant_ids = authorized_tenant_tree,
        .writable_tenant_ids = authorized_write_tenants,
    }));
auto orders = runtime.repository<order>(request, "primary");
```

The application/IAM layer resolves parent–child tenant and department IDs
from its own hierarchy and grants; the ORM does not assume a particular
tenant or organization table. Pass the authorized tenant IDs in
`tenant_scope` and the authorized department IDs in
`data_permission_scope::partition_ids`. The policies are intersected, not
unioned, so a parent tenant cannot see an unauthorized department in a child
tenant. Use distinct read and write tenant sets; when department write rights
differ, set `data_permission_scope::writable_partition_ids` as well (an empty
vector denies writes). INSERT must target a writable
tenant and the tenant column is immutable on UPDATE. In strict mode, INSERT
also validates marked `DATA_PARTITION` (and enabled `DATA_OWNER`) columns
against the request scope. Empty sets deny access.

Strict mode accepts single-table parameterized typed CRUD and XML statements,
including dynamic `WHERE`, `OR`, sorting and paging. SQL constructs that cannot
be safely scoped by this policy (cross-table JOIN/UNION, nested SELECT,
INSERT SELECT, upsert, or raw mutation of the tenant column) fail closed.
Transactions preserve the same scope snapshot. Do not route arbitrary SQL or
schema maintenance through a tenant-scoped repository. Only a separately
controlled maintenance path may run unscoped SQL.

## Upsert semantics

- `save_or_update` is identity-based and may perform a read before insert/update.
- `upsert` is one provider-native atomic statement.
- MySQL uses its native duplicate-key syntax.
- PostgreSQL uses `ON CONFLICT`.
- Batch APIs report `batch_index` and `item_index` when an item fails.

## Sharding

Sharding is opt-in. A non-sharded managed service continues to produce the same `application_repository<T>` API. Sharded gateways route by shard key, reject implicit scatter-gather writes and keep a single-shard transaction on one physical connection/table. Cross-shard transactions are not provided.

Application configuration builds the shard catalog and named data-source topology. Business code still calls `repository<T>`; it does not switch to a provider-specific Mapper.

## Internal APIs

`database_session<Client, ResultAdapter>` is intentionally low-level. Its public surface is limited to raw `query`/`execute`, transaction control, `dialect()`, `underlying()` and observed raw execution overloads. Typed model operations belong to `mapper<T>`. Application code must not construct or retain a raw session.

## Module visibility

Use explicit imports. Transitive imports are not a visibility guarantee:

```cpp
import std;
import cnetmod.application;
import cnetmod.orm;
```

Infrastructure code using `database_session` directly must explicitly import `cnetmod.orm.database_session`.

## Verification

After changing ORM interfaces or this document:

```bash
python tools/check_orm_docs.py
python tools/generate_agents.py
python tools/generate_agents.py --check
cmake --build build
ctest --test-dir build --output-on-failure --no-tests=error
```

## Source of truth

- `src/application/application_runtime.cppm`
- `src/application/orm_repository.cppm`
- `src/database/orm/repository_impl.cppm`
- `src/database/orm/mapper.cppm`
- `src/database/orm/database_session.cppm`
- `src/database/orm/session_gateway.cppm`
- `src/database/orm/query_wrapper.cppm`
- `src/database/orm/xml_mapper_registry.cppm`
