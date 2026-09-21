# ORM 模型定义 / CRUD / 迁移

> cnetmod 协议无关 SQL ORM，支持 MySQL / PostgreSQL。
> 模块: `import cnetmod.protocol.mysql;` + `#include <cnetmod/orm.hpp>`

## 核心原则

- 模型定义用 `CNETMOD_MODEL` + `CNETMOD_FIELD` 宏（编译期反射）
- CRUD 操作通过 Application 的 `repository<T>`，底层映射由 `mapper<T>` 完成
- 流式查询用 `query_wrapper<T>`（支持成员指针类型安全）
- DDL 迁移用 `mysql_synchronize_schema<T>()`
- XML mapper 提供 MyBatis 风格动态 SQL

## 1. 模型定义

```cpp
#include <cnetmod/orm.hpp>
import std;
import cnetmod.protocol.mysql;

struct User {
    std::int64_t id = 0;
    std::string name;
    std::optional<std::string> email;
    int status = 0;
    std::time_t created_at = 0;
};

CNETMOD_MODEL(User, "users",
    CNETMOD_FIELD(id, "id", bigint, PK | AUTO_INC),
    CNETMOD_FIELD(name, "name", varchar),
    CNETMOD_FIELD(email, "email", varchar, NULLABLE),
    CNETMOD_FIELD(status, "status", int_),
    CNETMOD_FIELD(created_at, "created_at", timestamp, NULLABLE))
```

### CNETMOD_FIELD 参数

`CNETMOD_FIELD(member, "col", type [, flags [, strategy]])`

列类型后缀: `bigint`, `int_`, `varchar`, `text`, `double_`, `char_`, `timestamp`, `tinyint`, `boolean_`

### 字段标志

| 宏 | 含义 | 宏 | 含义 |
|----|------|----|------|
| `PK` | 主键 | `VERSION` | 乐观锁版本号 |
| `AUTO_INC` | 自增 | `LOGIC_DELETE` | 软删除标记 |
| `NULLABLE` | 允许 NULL | `FILL_INSERT` | 插入时自动填充 |
| `UNIQUE_KEY` | 唯一约束 | `FILL_INSERT_UPDATE` | 插入+更新填充 |
| `TENANT_ID` | 多租户字段 | | |

标志可组合: `PK | AUTO_INC`。

## 2. ID 生成策略

### UUID 主键

```cpp
struct Tag { orm::uuid id; std::string name; };
CNETMOD_MODEL(Tag, "tags",
    CNETMOD_FIELD(id, "id", char_, UUID_PK_FLAGS, UUID_PK_STRATEGY),
    CNETMOD_FIELD(name, "name", varchar))
```

- `orm::uuid` — 128 位 UUID，`to_string()` / `from_string()`
- `uuid_v4()` — 生成随机 UUID v4
- DDL 生成 `CHAR(36)`

### Snowflake 主键

```cpp
struct Event { std::int64_t id = 0; std::string title; };
CNETMOD_MODEL(Event, "events",
    CNETMOD_FIELD(id, "id", bigint, SNOWFLAKE_PK_FLAGS, SNOWFLAKE_PK_STRATEGY),
    CNETMOD_FIELD(title, "title", varchar))
```

- `snowflake_generator(uint16_t machine_id)` — 构造（0~1023）
- `next_id() -> int64_t` — 生成 ID（非线程安全）

## 3. Application Repository 与 Mapper

Application 负责从连接池创建 `session_gateway`，业务只依赖统一的
`orm::repository<T, Gateway>`。Repository 获取连接租约并管理事务，Mapper
负责类型化 SQL、结果映射和错误传播；业务代码不直接构造
`database_session`，也不依赖 MySQL/ PostgreSQL 专属 ORM 门面。

```cpp
auto users = runtime.repository<User>("primary");
auto created = co_await users->save(User{.name = "Alice"});
auto page = co_await users->page(query_wrapper<User>{}.eq(&User::status, 1), 1, 20);

auto result = co_await users->transaction<std::size_t>(
    [](auto& unit) -> task<std::expected<std::size_t, orm_error>> {
        auto user_mapper = unit.template mapper<User>();
        auto first = co_await user_mapper.insert(...);
        if (!first) co_return std::unexpected(first.error());
        auto order_mapper = unit.template mapper<Order>();
        auto second = co_await order_mapper.insert(...);
        if (!second) co_return std::unexpected(second.error());
        co_return std::size_t{2};
    });
```

`repository<T>` 提供 MyBatis-Plus 风格的 `get_by_id`、严格 `get_one`、
`list`、`page`、`save`、`save_batch`、`save_or_update`、`update_by_id`、
`remove_by_id`、`exists`、投影查询和流式查询。`get_one` 默认要求最多一条；
多条匹配返回 `too_many_results`，不会静默取第一条。所有数据库失败保留
SQLSTATE、原生错误号和分类错误码。

## SQL 拦截器链

`interceptor_chain` 是 ORM SQL 的统一扩展点，适合做租户条件、审计字段、
安全策略和观测标签等参数化改写。raw `query()`/`execute()`、参数化语句、typed CRUD
和 streaming 查询都经过同一个 prepare 阶段；事务控制语句也会按 `execute` 操作分类
进入该链，因此自定义拦截器必须显式放行不需要改写的语句。

```cpp
auto interceptors = std::make_shared<orm::interceptor_chain>();
interceptors->add("tenant", 100,
    [](orm::sql_operation operation, orm::intercepted_statement statement)
        -> std::expected<orm::intercepted_statement, std::string> {
        if (operation == orm::sql_operation::query)
            statement.sql += " /* tenant policy */";
        return statement;
    });
interceptors->freeze();

orm::database_session db{client, orm::sql_dialect::mysql, interceptors};
```

拦截器按 `priority` 升序执行；名称必须唯一，`freeze()` 后不能再注册，未冻结的链
不能应用。回调返回错误时，当前 ORM 操作会在访问数据库前失败，错误文本包含拦截器
名称。回调拥有 `intercepted_statement` 的 SQL 和参数，可同时修改二者，但必须继续
保持占位符与参数数量一致。建议在 Application 构建阶段完成注册和冻结，并把冻结后的
`std::shared_ptr<const interceptor_chain>` 注入所有 Session。

## Mapper 与 Repository 分层

`orm::mapper<T, Session>` 是类型化 Model 映射层，对应 MyBatis-Plus 的
`BaseMapper<T>`。它负责字段映射、Wrapper 转 SQL、结果映射和数据库错误传递，
但不获取连接池租约，也不负责应用生命周期。

`orm::repository<T, Gateway>` 是应用持久化门面，对应 MyBatis-Plus 的
`IService<T>/ServiceImpl<T>`。它通过 Gateway 获取 Session，再委托 Mapper 完成
查询、写入、批量、分页和流式操作。业务 Service 只编排多个 Repository，不写 SQL。

`database_session` 只属于 Mapper/Gateway 内部执行边界；Application 代码不应直接持有它。

## Repository 门面

Application 业务代码应使用由 `session_gateway` 支持的 `repository<T, Gateway>`：

```cpp
orm::repository<User, decltype(gateway)> users{gateway};
auto user = co_await users.get_by_id(orm::param_value::from_int(id));
auto page = co_await users.page(1, 20, query);
auto saved = co_await users.save_or_update(entity);
```

提供 `get_by_id`、`get_one`、`list`、`list_by_ids`、`exists`、`page`、
`select_maps`、`select_objects`、`page_maps`、`save`、`save_batch`、
`save_or_update`、`save_or_update_batch`、`upsert`、`upsert_batch`、
`update_by_id`、`update_by_wrapper`、`remove` 和 `remove_by_id`。
批量方法按 `batch_size` 分段，每段独立开启、提交或回滚事务；首个失败结果原样返回。
所有方法继续返回
`model_result<T>` 或 `page_result<T>`，因此空结果、数据库错误和框架错误不会被门面
压扁成 `optional` 或布尔值。连接池租约由 Gateway 负责，模型事务、批处理和拦截器由
Repository 负责；Repository 不持有裸连接。

`repository::upsert()` 和 `upsert_batch()` 使用数据库原生冲突语义：MySQL 生成
`ON DUPLICATE KEY UPDATE`，PostgreSQL 生成 `ON CONFLICT (...) DO UPDATE`。它们与
`save_or_update()` 的“先查后写”语义不同，适合高并发写入和唯一键冲突场景。

Repository 通过 `session_gateway` 为每次操作持有租约，自动安装多租户、逻辑删除、字段
填充和 SQL 安全策略，并管理模型级写事务。Gateway 只负责连接租约和 session 生命周期，
Repository 负责模型事务，因此失败的写操作先回滚，再原样返回 `model_result<T>`
中的 SQLSTATE、原生错误号和批次位置。框架不再导出重复的通用 CRUD Service 门面；
业务 Service 只负责业务编排，不负责 SQL。

`automatic_interceptor_options` 统一控制 SQL 阶段的多租户、逻辑删除和安全检查，以及
模型阶段的字段填充和乐观锁。五项策略默认启用；显式关闭 `field_fill` 后 Session 不再
修改自动填充字段，关闭 `optimistic_lock` 后版本列按普通字段更新，不再生成版本谓词、
自动递增或把零影响行分类为冲突。直接构造且未安装自动流水线的低层 Session 保留历史
默认行为。

Application 的 MySQL 集成可直接创建完整门面；PostgreSQL 使用同一套
`Mapper/Repository` 契约，只替换 dialect、client 和 pool gateway：

```cpp
auto users = runtime.repository<User>("primary");

auto page = co_await users.page(1, 20, query);
auto saved = co_await users.upsert(user);
```

```cpp
auto users = runtime.repository<User>(
    "primary", {}, application::database_provider::postgresql);
auto page = co_await users->page(1, 20, query);
```

MySQL Repository 的 `for_each()` 使用 MySQL 多函数执行协议：查询只提交一次，
`read_some_rows()` 在 handler 完成后才读取下一批。取消、deadline、handler 失败或
提前销毁会关闭未读完的连接；连接池不会复用带残留结果包的连接。

Repository 还公开同样的 `list_by_ids`、`exists`、投影查询/分页和 `for_each_map` 能力；
这些方法仍复用同一个 Session、租约和自动拦截器配置。

MyBatis-Plus `BaseMapper` 对应的公开方法全部位于 provider-neutral
`mapper<T, Session>`；它们返回 `model_result` 或 `page_result`，数据库错误不会被
转换为空 optional/vector/bool。协议模块不再导出 MySQL/PostgreSQL 各自的 ORM 门面。

## 分库分表

`shard_catalog` 把稳定的 `shard_key` 同时映射到具名数据库实例和经过校验的物理表。
默认 `hash_shard_strategy` 使用确定性哈希，不依赖进程随机种子；也可实现
`shard_strategy` 注入范围、目录或租户路由。目录在 `freeze()` 后只读，启动前拒绝空拓扑、
重复实例和非法 SQL 标识符。

```cpp
auto catalog = std::make_shared<orm::shard_catalog>();
catalog->add_database("orders-0");
catalog->add_database("orders-1");
catalog->freeze("orders", 64,
    std::make_shared<orm::hash_shard_strategy>());

auto gateway = application::make_mysql_sharded_session_gateway(
    host.services(), catalog);

auto result = co_await gateway->write<OrderId>(orm::shard_key{tenant_id},
    [&](auto& session) -> task<std::expected<OrderId, std::string>> {
        Order order{/* ... */};
        auto inserted = co_await session.insert(order);
        if (inserted.is_err())
            co_return std::unexpected(inserted.error_msg);
        co_return order.id;
    });
```

物理表名形如 `orders_00`～`orders_63`，所有 CRUD 和 wrapper SQL 都使用路由结果，
值仍由参数绑定传输。`write()` 只向回调暴露已经固定到一个库和一张表的 session，并在
同一连接上开启、提交或回滚事务，因此不会静默产生跨分片事务。

跨分片能力必须显式调用：

- `scatter_read<T>()` 访问全部物理分片并保留每个分片的成功或失败结果。
- `scatter_gather<Item, Result>()` 在完整 scatter 结果上调用业务提供的合并器；排序、分页、
  聚合及是否接受部分结果均由合并器决定。
- `distributed_transaction<T>()` 在单数据库时使用普通事务，在多个 MySQL 实例时使用
  XA 两阶段提交，并把固定到物理表的 `distributed_session_context` 交给回调。
- scatter 回调抛出的异常会转换为对应分片的失败结果；分布式事务回调抛出的异常会转换
  为事务错误并回滚已经启动的分支，不会越过 ORM 边界泄漏异常。
- 任一提交返回失败时会报告结果不确定；生产系统应配合 MySQL `XA RECOVER`、事务日志和
  运维补偿处理进程崩溃或网络分区，不能把 XA 当作无故障的本地事务。

Application 中先配置多个具名 `mysql_service`。工厂创建 gateway 时验证 catalog 引用的
每个实例均已注册；连接池的启动、健康恢复和逆序停机仍由 Application 管理。

调用 `enable_auto_configuration()` 后，也可以通过 `orm.sharding.enabled` 自动创建具名网关：

```json
{
  "orm": {
    "sharding": {
      "enabled": true,
      "topologies": {
        "orders": {
          "logical_table": "orders",
          "table_count": 64,
          "databases": ["orders-0", "orders-1"],
          "scatter_gather": true,
          "distributed_transactions": true
        }
      }
    }
  }
}
```

通过 `host.services().require<application::mysql_sharded_session_gateway>("orders")`
取得对应网关。`enabled` 默认是 `false`；关闭时仍使用普通 `database_session`，不会创建
catalog、分片网关或改变原有 MySQL 服务。

## ORM JSON：纯 import、零实体样板

`CNETMOD_MODEL` 的字段元数据可直接用于 JSON，不需要 `#include <nlohmann/json.hpp>`，也不需要
`NLOHMANN_DEFINE_TYPE_*` 宏：

```cpp
import nlohmann.json;
import cnetmod.orm;

auto payload = orm::to_json(article).dump();
auto decoded = orm::from_json<Article>(nlohmann::json::parse(payload, nullptr, false));
```

`from_json<T>` 返回 `std::expected<T, std::string>`；缺失字段保留模型默认值，类型不匹配返回错误。
当前覆盖数值、布尔、字符串、枚举与可空字段；日期/时间和二进制字段需要显式边界格式后再加入。

## XML ResultMap

`mapper_registry` 会加载 `<resultMap>`，并支持 `namespace.id` 查询：

```xml
<resultMap id="UserMap" type="User" autoMapping="false">
  <id property="id" column="id" jdbcType="BIGINT"/>
  <result property="displayName" column="display_name" jdbcType="VARCHAR"/>
</resultMap>
<select id="findById" resultMap="UserMap">SELECT ...</select>
```

`<id>`、`<result>`、`<association>` 与 `<collection>` 的映射元数据已解析并注册。

`result_map_applier` 提供显式对象图材料化：连接查询可按根和 collection 的
`<id>` 去重聚合。XML 语句仍通过 `repository<T>::select_xml()` 执行；框架不会在
属性访问时隐式发起 N+1 查询。例如：

```xml
<resultMap id="UserGraph" type="User">
  <id property="id" column="user_id"/>
  <result property="name" column="user_name"/>
  <!-- JOIN 查询：同一 user_id 的多行会聚合为一个用户和多个 roles -->
  <collection property="roles" resultMap="RoleMap"/>
</resultMap>

<resultMap id="UserWithOrders" type="User">
  <id property="id" column="id"/>
  <!-- 嵌套查询：父行 id 会作为 #{id} 传给 findOrdersByUserId -->
  <collection property="orders" column="id" select="findOrdersByUserId" resultMap="OrderMap"/>
</resultMap>
<select id="findOrdersByUserId" resultMap="OrderMap">
  SELECT id, user_id, total FROM orders WHERE user_id = #{id}
</select>
```

嵌套 select 由业务在同一个 Repository 事务中显式编排。根对象的标量字段可通过
`from_mapped_objects<T>()` 投影到 `CNETMOD_MODEL` DTO；关联和集合由
`xml_object_graph_binder<T>` 显式绑定到真实的 C++ 成员，避免 XML 字符串猜测成员布局。
例如为 `User` 声明一次绑定：

```cpp
namespace cnetmod::orm {
template <> struct xml_object_graph_binder<User> {
    static void bind(User& user, const mapped_object& source) {
        user.team = mapped_association_as<Team>(source, "team");
        user.roles = mapped_collection_as<Role>(source, "roles");
    }
};
} // namespace cnetmod::orm
```

`lazy_relation<T>` 可用于 C++ 业务层显式协程按需加载，访问必须 `co_await get()`，不会在普通属性访问中阻塞。

## 4. mapper<T> — MyBatis-Plus BaseMapper 风格

```cpp
orm::mapper<User, orm::mysql_database_session> mapper(session);

co_await mapper.insert(user);
auto selected = co_await mapper.select_by_id(42);
auto list = co_await mapper.select_list();
auto cnt = co_await mapper.select_count();
auto exists = co_await mapper.exists_by_id(42);
co_await mapper.update_by_id(user);
co_await mapper.update_batch_by_id(users);
co_await mapper.save_or_update(user);
co_await mapper.upsert_batch(users);
co_await mapper.delete_by_id(42);
co_await mapper.delete_batch_ids(id_vec);
auto page = co_await mapper.select_page(1, 20, wrapper);
```

标准方法统一返回 `model_result<T>` 或 `page_result<T>`，包括 `insert`、
`insert_batch`、`update_by_id`、`update_batch_by_id`、`save_or_update`、
`save_or_update_batch`、`upsert_result`、`upsert_batch`、`delete_by_id`、
`delete_batch_ids`、`select_by_id`、`select_batch_ids`、`select_by_map`、
`select_list`、`select_one`、`select_count`、`exists_by_id`、`select_page`、
`select_maps`、`select_objects`、`select_maps_page`、`delete_by_wrapper` 和
`update_by_wrapper`。它们不会把数据库失败压扁成空值或 `false`。

旧返回语义仅通过显式 `legacy_*` 名称保留，供迁移存量代码使用；新的标准方法不再
暴露 `optional<T>`、裸 `vector<T>` 或裸布尔错误语义。

## 5. query_wrapper<T> — 流式查询

`update_wrapper<T>` 支持参数化的 `set_increment(column, value)` 和
`set_decrement(column, value)`，生成 `column = column +/- {}`，增量值仍通过绑定参数
传输，不接受任意 SQL 表达式。这样可以安全覆盖计数器和库存扣减等更新场景。

`database_session::for_each()` 和 `repository::for_each()` 提供有界流式消费：每次只
读取 `stream_options::batch_size` 行，等待 handler 完成后才读取下一页，并可设置
`max_rows`。带 `cancel_token` 的重载会在下一页和每行之间检查取消，handler 返回错误
会立即终止。它是跨数据库的分页流；需要 PostgreSQL 线级 portal 时，应直接使用
PostgreSQL 客户端的 `query_batches()`。

`for_each_map()` 为动态投影提供相同的背压、`max_rows`、取消和 deadline 语义。
MySQL 应用门面默认切换到 `mysql_stream_strategy`，不使用 `LIMIT/OFFSET`；通用
`database_session::open_cursor()` 保留为跨协议的页式 fallback。

Wrapper 的子查询使用 `subquery{sql, parameters}` 传递：`in_subquery`、
`not_in_subquery`、`exists`、`not_exists` 以及 `eq_subquery`、`ne_subquery`、
`gt_subquery`、`ge_subquery`、`lt_subquery`、`le_subquery` 都会把参数并入当前语句的
绑定序列。子查询使用中立 `{}` 占位符；构建 PostgreSQL SQL 时会按外层参数位置重新
编号成 `$N`，不会通过 `raw()` 绕过参数绑定。

```cpp
// 成员指针（类型安全）
auto qw = orm::query_wrapper<User>{}
    .eq(&User::status, 1)
    .contains(&User::name, "Alice")
    .order_by_desc(&User::created_at)
    .limit(10);

// 字符串列名
auto qw2 = orm::query_wrapper<User>{}
    .eq("status", 1)
    .like("name", "%Alice%")
    .between("age", 18, 65)
    .in("role", std::vector<std::string>{"admin", "editor"})
    .order_by_desc("created_at")
    .limit(20).offset(40);
```

### 条件方法

| 方法 | SQL | 方法 | SQL |
|------|-----|------|-----|
| `eq` / `ne` | `=` / `!=` | `like` / `not_like` | `LIKE` |
| `gt` / `ge` / `lt` / `le` | `>` / `>=` / `<` / `<=` | `is_null` / `is_not_null` | `IS NULL` |
| `in` / `not_in` | `IN` / `NOT IN` | `between` / `not_between` | `BETWEEN` |
| `starts_with` / `ends_with` / `contains` | LIKE 变体 | `is_true` / `is_false` | `IS TRUE/FALSE` |
| `raw(sql)` | 原始 SQL（慎用） | `when(bool, fn)` | 条件执行 |

### 逻辑 / 排序 / 聚合

`and_()` / `or_()` 切换连接符 · `and_(nested)` / `or_(nested)` 嵌套条件组 · `order_by_asc` / `order_by_desc` · `limit` / `offset` · `select({...})` 指定列 · `group_by` · `having` · `inner_join` / `left_join` / `right_join` / `full_outer_join` · `select_count` / `select_sum` / `select_avg` / `select_min` / `select_max`

### 构建 SQL

```cpp
auto [sql, params] = qw.build_select_sql();          // MySQL 默认
auto [sql, params] = qw.build_select_sql(sql_dialect::postgresql);
auto [sql, params] = qw.build_count_sql();
auto [sql, params] = qw.build_delete_sql();
auto [sql, params] = qw.build_update_sql(entity);
```

### update_wrapper<T>

```cpp
auto uw = orm::update_wrapper<User>{}
    .set(&User::name, "Bob")
    .eq(&User::id, 42);
co_await mapper.update_by_wrapper(uw);
```

## 6. 查询构建器

```cpp
auto qb = orm::mysql_select<Article>()
    .where("`status` = {}", {orm::param_value::from_int(1)})
    .order_by("`view_count` DESC")
    .limit(10);
auto result = co_await db.find(qb);

auto del = orm::mysql_delete<Article>()
    .where("`status` = {}", {orm::param_value::from_int(0)});
co_await db.remove(del);
```

## 7. DDL 自动迁移

```cpp
auto result = co_await orm::mysql_synchronize_schema<Product>(cli);
if (result.is_err()) { /* handle */ }
if (result.created)
    std::println("表已创建");
else
    std::println("应用了 {} 项变更", result.diff.changes.size());
```

对比 C++ 模型与数据库表结构，自动 ADD / DROP / MODIFY 列。

## 8. MyBatis 风格 XML Mapper（动态 SQL）

XML mapper 提供 MyBatis 风格的 SQL 定义与动态 SQL 能力：SQL 写在 `.xml` 文件中，
运行时由 `mapper_registry` 加载、`dynamic_sql_processor` 根据参数上下文渲染为
最终 SQL 并执行。

### XML 文件格式

根标签必须是 `<mapper>` 且必须带 `namespace` 属性；语句标签为
`<select>` / `<insert>` / `<update>` / `<delete>`（各需 `id` 属性），
可复用片段用 `<sql id="...">` 定义、`<include refid="..."/>` 引用。

```xml
<?xml version="1.0" encoding="UTF-8"?>
<mapper namespace="UserMapper">

    <!-- 可复用 SQL 片段 -->
    <sql id="columns">
        `id`, `name`, `email`, `status`, `created_at`
    </sql>

    <!-- 简单查询 -->
    <select id="findById">
        SELECT <include refid="columns"/>
        FROM `users`
        WHERE `id` = #{id}
    </select>

    <!-- 动态条件查询 -->
    <select id="findByCondition">
        SELECT <include refid="columns"/>
        FROM `users`
        <where>
            <if test="name != null and name != ''">
                AND `name` = #{name}
            </if>
            <if test="status != null">
                AND `status` = #{status}
            </if>
        </where>
        ORDER BY `id` DESC
    </select>

    <insert id="insertUser">
        INSERT INTO `users` (`name`, `email`, `status`, `created_at`)
        VALUES (#{name}, #{email}, #{status}, #{created_at})
    </insert>

    <!-- 动态 SET（自动补 SET 关键字、去尾部逗号） -->
    <update id="updateSelective">
        UPDATE `users`
        <set>
            <if test="name != null">`name` = #{name},</if>
            <if test="email != null">`email` = #{email},</if>
        </set>
        WHERE `id` = #{id}
    </update>

    <delete id="deleteByStatus">
        DELETE FROM `users` WHERE `status` = #{status}
    </delete>
</mapper>
```

### 支持的标签

| 标签 | 用途 | 属性 |
|------|------|------|
| `<mapper>` | 根元素 | `namespace`（必填） |
| `<sql>` | 可复用 SQL 片段 | `id` |
| `<select>` / `<insert>` / `<update>` / `<delete>` | 语句定义 | `id` |
| `<include>` | 引入 `<sql>` 片段 | `refid` |
| `<if>` | 条件包含 | `test`（布尔表达式） |
| `<where>` | 自动补 `WHERE`、去掉首部 `AND`/`OR` | — |
| `<set>` | 自动补 `SET`、去掉尾部逗号 | — |
| `<trim>` | 前后缀增删 | `prefix`、`suffix`、`prefixOverrides`、`suffixOverrides` |
| `<foreach>` | 遍历集合 | `collection`、`item`、`open`、`close`、`separator` |
| `<choose>` / `<when>` / `<otherwise>` | 多分支（首个匹配的 `when` 生效） | `when` 带 `test` |
| `<bind>` | 绑定表达式到新变量 | `name`、`value` |

语句标签读取 `id`；`<select>` 支持 `resultMap` 或 `resultType`（两者互斥，加载时
校验）；所有语句都可声明 `parameterType`，并可通过
`statement_result_type()` / `statement_parameter_type()` 查询元数据。
`<foreach>` 支持可选的零基 `index` 属性，迭代期间作为参数上下文变量绑定。XML 中 `>`、`<`、`&` 需写成
`&gt;`、`&lt;`、`&amp;`。

### namespace 与语句 ID

- `<mapper namespace="UserMapper">` + `<select id="findById">` → 语句全限定 ID
  `UserMapper.findById`。
- `registry.find_statement()` 同时支持全限定 ID（`"namespace.id"`）和裸 ID（`"id"`，
  全局唯一时可用），C++ 调用处写法相同。
- namespace 与 C++ 接口/类**无绑定关系**，它只是语句 ID 的命名空间前缀；
  `<include refid>` 只能引用同一 namespace 内的 `<sql>` 片段。
- 一个 `mapper_registry` 可加载多个不同 namespace 的 mapper 文件。

### 参数占位符

| 语法 | 行为 |
|------|------|
| `#{name}` | 参数化占位符（安全，值进入参数列表后由 SQL 格式化层转义） |
| `${name}` | 直接字符串替换（有注入风险，用于 ORDER BY / GROUP BY / 表名等无法参数化的位置） |

- 支持点路径访问集合元素属性：`#{user.name}`、`${cond.field}`。
- 参数值来自 `param_context`（map、模型对象或集合，见「注册与加载」）。
- `#{property,jdbcType=...,javaType=...,typeHandler=...,mode=...,numericScale=...}`
  会绑定 `property`，并保留修饰元数据。XML Mapper 生成统一的
  `parameterized_query`：MySQL 在协议适配器中安全编码参数，PostgreSQL 自动转换为
  `$1...$N` 占位符；Repository 与业务层不拼接参数值。
- `${}` 只做直接替换，不能用来传递 JDBC 修饰符。

### test 表达式

`<if test>` / `<when test>` / `<bind value>` 使用内置表达式引擎，支持：

- 比较：`==`、`!=`、`<`、`>`、`<=`、`>=`（XML 中写 `&lt;` `&gt;`）
- 逻辑：`and`、`or`、`not`（不支持 `&&` / `||`）
- 算术：`+`、`-`、`*`、`/`、`%`，括号分组
- 字面量：整数、浮点、`'单引号'` 或 `"双引号"` 字符串、`true`、`false`、`null`
- 属性路径：`a.b.c` 逐级解析

示例：`test="name != null and name != ''"`、`test="limit &gt; 0"`、
`test="role == 'admin'"`、`test="includeOrders == true"`。

### 动态 SQL 示例

**foreach — IN 子句 / 批量插入**：

```xml
<!-- 集合由 param_context::add_collection("ids", ...) 提供 -->
<select id="findByIds">
    SELECT <include refid="columns"/>
    FROM `users`
    WHERE `id` IN
    <foreach collection="ids" item="id" open="(" close=")" separator=",">
        #{id}
    </foreach>
</select>

<!-- 批量插入：每个元素是带字段的 param_context -->
<insert id="batchInsert">
    INSERT INTO `users` (`name`, `email`, `status`, `created_at`)
    VALUES
    <foreach collection="users" item="user" separator=",">
        (#{user.name}, #{user.email}, #{user.status}, #{user.created_at})
    </foreach>
</insert>
```

**choose/when/otherwise**：

```xml
<select id="findByRole">
    SELECT <include refid="columns"/>
    FROM `users`
    <where>
        <choose>
            <when test="role == 'admin'">AND `status` = 1</when>
            <when test="role == 'moderator'">AND `status` IN (1, 2)</when>
            <otherwise>AND `status` = 0</otherwise>
        </choose>
    </where>
</select>
```

**trim + bind**：

```xml
<select id="advancedFilter">
    SELECT <include refid="columns"/>
    FROM `users`
    <where>
        <!-- 输出 ( `name` LIKE ? OR `email` LIKE ? )，去掉首部 OR -->
        <trim prefix="(" suffix=")" prefixOverrides="OR">
            <if test="namePattern != null and namePattern != ''">
                OR `name` LIKE #{namePattern}
            </if>
            <if test="emailPattern != null and emailPattern != ''">
                OR `email` LIKE #{emailPattern}
            </if>
        </trim>
    </where>
</select>

<select id="dynamicTableQuery">
    SELECT * FROM ${tableName}
    <where>
        <foreach collection="filters" item="filter" separator="AND">
            <bind name="fieldName" value="filter.field"/>
            <bind name="fieldValue" value="filter.value"/>
            ${fieldName} = #{fieldValue}
        </foreach>
    </where>
</select>
```

### 结果集映射

`repository<T>::select_xml()` 按列名匹配 `CNETMOD_MODEL` 字段，返回与普通 CRUD
完全相同的 `model_result<T>`，包括 SQLSTATE、数据库原生错误号、影响行数和生成主键。
`get_one_xml()` 默认执行严格单条语义：多行不是“取第一条”，而是返回
`result_out_of_range`。XML `resultMap` 元数据和 `result_map_applier` 仍可用于显式的
动态对象图材料化；业务 Repository 不暴露数据库连接或原始 Session。

### 注册与加载

**mapper_registry API**（同步，返回 `std::expected<void, std::string>`）：

| 方法 | 说明 |
|------|------|
| `load_file(path)` | 加载单个 `.xml` 文件 |
| `load_xml(content)` | 从字符串加载（如嵌入式资源） |
| `load_directory(dir)` | 加载目录下所有 `.xml` 文件 |
| `find_statement(id)` | 查找语句节点（`"Ns.id"` 或裸 `"id"`） |
| `statement_type(id)` | 返回语句标签名（select/insert/update/delete） |

**Repository XML API**（与普通 CRUD 共用同一个 Repository）：

```cpp
auto users = runtime.repository<User>("primary");

auto list = co_await users->select_xml(
    registry, "UserMapper.findByCondition", parameters);
auto one = co_await users->get_one_xml(
    registry, "UserMapper.findById", parameters);
auto changed = co_await users->execute_xml(
    registry, "UserMapper.updateSelective", parameters);
```

三个入口都经过相同的连接池租约、自动拦截器、错误映射、OTEL 和事务边界。
`execute_xml()` 自动开启写事务并在失败时回滚。跨模型事务中使用
`unit.xml<User>(registry)`，它与 `unit.mapper<Order>()` 共享同一连接和事务。

```cpp
auto committed = co_await users->transaction<void>(
    [&](auto& unit) -> task<std::expected<void, std::string>> {
        auto user_xml = unit.template xml<User>(registry);
        auto selected = co_await user_xml.select(
            "UserMapper.findById", parameters);
        if (selected.is_err())
            co_return std::unexpected(selected.error_msg);

        auto orders = unit.template mapper<Order>();
        auto inserted = co_await orders.insert(order);
        if (inserted.is_err())
            co_return std::unexpected(inserted.error_msg);
        co_return {};
    });
```

**参数传递（param_context）**：

```cpp
// 1. 类型化参数；框架保留 uint64、DATETIME 等原始 SQL 类型
orm::param_context ctx;
ctx.set("name", std::string_view{"Alice"});
ctx.set("status", std::int64_t{1});
ctx.set("limit", std::uint64_t{10});
ctx.set("created_at", orm::calendar_datetime{2026, 9, 17, 8, 31, 57, 0});

// 2. 模型对象作为参数源（按字段名映射）
auto ctx2 = orm::param_context::from_model(user);

// 3. 集合参数（供 <foreach> 使用）
auto ctx3 = orm::param_context::from_map({});
std::vector<orm::param_context> items;
items.push_back(orm::param_context::from_map({{"id", orm::param_value::from_int(1)}}));
items.push_back(orm::param_context::from_map({{"id", orm::param_value::from_int(2)}}));
ctx3.add_collection("ids", std::move(items));
```

**完整示例**（Application Repository + XML）：

```cpp
import std;
import cnetmod.application;
import cnetmod.coro.task;
#include <cnetmod/orm.hpp>

using namespace cnetmod;
using namespace cnetmod::orm;

struct User
{
    std::int64_t id = 0;
    std::string name;
    std::optional<std::string> email;
    int status = 0;
    std::time_t created_at = 0;
};

CNETMOD_MODEL(User, "users",
    CNETMOD_FIELD(id, "id", bigint, PK | AUTO_INC),
    CNETMOD_FIELD(name, "name", varchar),
    CNETMOD_FIELD(email, "email", varchar, NULLABLE),
    CNETMOD_FIELD(status, "status", int_),
    CNETMOD_FIELD(created_at, "created_at", timestamp, NULLABLE))

auto work(application::application_runtime& runtime) -> task<void>
{
    mapper_registry registry;
    auto loaded = registry.load_file("mappers/user_mapper.xml");
    if (!loaded)
        co_return;

    auto users = runtime.repository<User>("primary");
    if (!users)
        co_return;

    param_context parameters;
    parameters.set("name", std::string{"Alice"});
    parameters.set("status", std::int64_t{1});
    auto found = co_await users->select_xml(
        registry, "UserMapper.findByCondition", parameters);

    auto context = param_context::from_map({});
    std::vector<param_context> ids;
    for (int i = 1; i <= 5; ++i)
        ids.push_back(param_context::from_map({{"id", param_value::from_int(i)}}));
    context.add_collection("ids", std::move(ids));
    auto selected = co_await users->select_xml(
        registry, "UserMapper.findByIds", context);
}
```

> **生产模式**：`mapper_registry` 在 Application 装配阶段加载并冻结语义，业务只保存
> Registry 的只读引用和 `repository<T>`。Repository 管理连接、事务和生命周期，XML
> 不创建第二套 Session/Pool，也不会绕过普通 ORM 的拦截器与错误契约。

## 9. 自动填充 / 软删除 / 多租户

### 自动填充
`CNETMOD_FIELD(created_at, "created_at", timestamp, FILL_INSERT)` — `fill_strategy`: `current_timestamp`, `current_date`, `current_time`, `uuid`, `custom`。`global_auto_fill_interceptor()` 获取全局实例。

### 软删除
`CNETMOD_FIELD(deleted, "deleted", tinyint, LOGIC_DELETE)` — `logical_delete_interceptor` 自动将 DELETE 转为 `UPDATE SET deleted=1`，SELECT 追加 `deleted=0`。`global_logical_delete_interceptor()`。

Nullable datetime markers use an explicit mode. Active rows are selected with
`deleted_at IS NULL`, and deletion writes `CURRENT_TIMESTAMP`:

```cpp
logical_delete_config config;
config.field_name = "deleted_at";
config.mode = logical_delete_mode::nullable_datetime;
config.touch_fields = {{"updated_at",
    logical_delete_touch_value::current_timestamp}};
logical_delete_interceptor interceptor{std::move(config)};
```

`touch_fields` 与逻辑删除标记在同一条 `UPDATE` 中更新，保持单语句原子性。字段名只能是
安全 SQL 标识符，赋值只能从 `current_timestamp`、`current_date`、`current_time`
枚举选择；不接受任意 SQL 表达式。重复字段、非法标识符或与删除标记重复会在配置
拦截器时抛出 `std::invalid_argument`。

### 多租户
`CNETMOD_FIELD(tenant_id, "tenant_id", bigint, TENANT_ID)` — `tenant_context::set_tenant_id(id)` 设置线程级租户；`tenant_guard guard(id)` RAII 守卫；`multi_tenant_interceptor` 自动注入条件。`global_multi_tenant_interceptor()`。

## 10. database_session<Client> — 协议无关会话

```cpp
template <asynchronous_database_client Client>
class database_session {
    explicit database_session(Client& client,
        sql_dialect dialect = sql_dialect::mysql);
    auto query(std::string_view sql) -> task<query_result>;
    auto execute(std::string_view sql) -> task<query_result>;
    auto execute(parameterized_query) -> task<query_result>;

    template <Model T> auto find_all() -> task<model_result<T>>;
    template <Model T> auto find_by_id(param_value) -> task<model_result<T>>;
    template <Model T> auto find_one_by(std::string_view, param_value)
        -> task<model_result<T>>;
    template <Model T> auto find_one(const query_wrapper<T>&,
        single_result_policy = single_result_policy::require_unique)
        -> task<model_result<T>>;
    template <Model T> auto find_first(const query_wrapper<T>&)
        -> task<model_result<T>>;
    template <Model T, typename Id> auto find_by_ids(std::span<const Id>)
        -> task<model_result<T>>;
    template <Model T> auto find_by_map(
        std::span<const std::pair<std::string, param_value>>)
        -> task<model_result<T>>;
    template <Model T> auto exists(const query_wrapper<T>&)
        -> task<model_result<bool>>;
    template <Model T> auto select_maps(const query_wrapper<T>&)
        -> task<model_result<projection_row>>;
    template <Model T> auto select_objects(const query_wrapper<T>&)
        -> task<model_result<field_value>>;
    template <Model T> auto page_maps(std::size_t, std::size_t,
        const query_wrapper<T>& = {}) -> task<page_result<projection_row>>;
    template <Model T> auto page(std::size_t, std::size_t,
        const query_wrapper<T>& = {}) -> task<page_result<T>>;
    template <Model T> auto prepare_select(const query_wrapper<T>&) const
        -> std::expected<parameterized_query, std::string>;
    template <Model T> auto for_each_map(const query_wrapper<T>&, Handler,
        stream_options = {}) -> task<std::expected<void, std::string>>;
    template <Model T> auto insert(T&) -> task<model_result<T>>;
    template <Model T> auto update(const T&) -> task<model_result<T>>;
    template <Model T> auto update_batch_by_id(std::span<const T>,
        std::size_t batch_size = 256) -> task<model_result<T>>;
    template <Model T> auto save_or_update(T&) -> task<model_result<T>>;
    template <Model T> auto save_or_update_batch(std::span<T>,
        std::size_t batch_size = 256) -> task<model_result<T>>;
    template <Model T> auto upsert(T&) -> task<model_result<T>>;
    template <Model T> auto upsert_batch(std::span<T>,
        std::size_t batch_size = 256) -> task<model_result<T>>;
    template <Model T> auto remove(const T&) -> task<model_result<T>>;
    template <Model T> auto remove_by(std::string_view, param_value)
        -> task<model_result<T>>;
    template <Model T> auto remove_by_id(param_value) -> task<model_result<T>>;
    template <Model T, typename Id> auto remove_by_ids(std::span<const Id>)
        -> task<model_result<T>>;
    template <Model T> auto remove_by_map(
        std::span<const std::pair<std::string, param_value>>)
        -> task<model_result<T>>;
    template <Model T> auto find(const query_wrapper<T>&) -> task<model_result<T>>;
    template <Model T> auto remove(const query_wrapper<T>&) -> task<model_result<T>>;
    template <Model T> auto remove(const query_wrapper<T>&, allow_full_table_t)
        -> task<model_result<T>>;
    template <Model T> auto update(const update_wrapper<T>&) -> task<model_result<T>>;
    template <Model T> auto update(const update_wrapper<T>&, allow_full_table_t)
        -> task<model_result<T>>;
    template <Model T> auto execute(const query_wrapper<T>&) -> task<model_result<T>>;
    template <Model T> auto execute(const update_wrapper<T>&) -> task<model_result<T>>;
    auto transaction(Func&&) -> task<query_result>;
    auto transaction(Func&&, isolation_level) -> task<query_result>;
};
```

MySQL failures retain both the native server error number (for example `1062`
for a duplicate key) and SQLSTATE. Prefer `error_code` for vendor-specific
classification and keep `sql_state` for portable error classes.

Model fields may use `std::optional<calendar_datetime>` for nullable
`DATETIME`/`TIMESTAMP` columns. Mapping a database datetime into an integral
Unix time treats the stored wall-clock fields as UTC and therefore does not
depend on the process or database-session timezone.

Use the public UTC conversion helpers instead of duplicating chrono calendar
arithmetic in mapper implementations:

```cpp
const auto now = cnetmod::unix_time_seconds();
const auto datetime = cnetmod::database::datetime_from_unix_seconds(now);
const auto seconds = cnetmod::database::unix_seconds_from_datetime(datetime);
```

Both conversion directions return `std::optional` where representation or SQL
NULL can fail. Keep that distinction through mapper and domain boundaries;
apply an application-specific sentinel such as `.value_or(0)` only at the
boundary that explicitly defines zero as its fallback value.

`database_session` is the protocol-independent execution context. It accepts
either a MySQL or PostgreSQL client and preserves the native wire client below
it. `model_result<T>` contains `data`, `affected_rows`, `last_insert_id`,
`error_msg`, `sql_state`, the native `error_code`, and a separate
`framework_error`. Native database diagnostics are never overwritten by ORM
cardinality or validation failures. Use `ok()` and `first()` to distinguish an
empty query from a failed operation.

Single-row reads are explicit. `find_one()` defaults to
`single_result_policy::require_unique`, requests at most two rows, and reports
`std::errc::result_out_of_range` when the predicate is ambiguous. Call
`find_first()` or pass `single_result_policy::first` only when choosing the
first matching row is intentional. `find_one_by()` uses the strict contract;
primary-key lookup uses the first-row policy because the schema owns uniqueness.
`exists()` returns `model_result<bool>` so an unavailable database is distinct
from a successful negative result.

Map filters accept only mapped columns and translate null values to `IS NULL`.
For reusable wrapper construction, `query_wrapper::all_eq()` applies the same
null policy. Dynamic projections use `projection_row`, an ordered map from
column label to typed `field_value`; `select_objects()` returns the first
projected column without converting it to text. `page_maps()` performs the
count and page queries on the same session and keeps diagnostics from either
operation.

`update_batch_by_id()` and `save_or_update_batch()` execute inside one local
transaction and roll back the successful prefix when a later item fails.
`batch_size` bounds each processing group and must be greater than zero.
`save_or_update()` inserts an unset auto-increment key; otherwise it checks the
primary key before choosing INSERT or UPDATE.

`page<T>()` is the typed counterpart to `page_maps()`. Both operations use the
same session for COUNT and SELECT, and return an empty page when the requested
page is outside the result range.

`remove_by_ids()` treats an empty ID span as a successful no-op. `remove_by_map()`
rejects an empty map and validates every key against model metadata. An empty
`update_wrapper` is rejected by default for the same reason as an empty DELETE;
pass `allow_full_table` only in an explicitly authorized maintenance path.

An empty conditional DELETE is rejected by default. A deliberate full-table
operation must pass the visible authorization tag through Mapper or Repository:

```cpp
orm::query_wrapper<User> all_users;
auto deleted = co_await users.remove(all_users, orm::allow_full_table);
```

## Application-managed repositories

Application code obtains the only supported persistence facade from the runtime.
The runtime owns pool leases, session creation, transaction boundaries and
provider selection; business services never receive a client, pool, gateway,
`database_session` or `io_context`.

```cpp
import cnetmod.application;
import cnetmod.orm;

auto users = runtime.repository<User>("primary");
if (!users)
    co_return std::unexpected(users.error());

auto active = co_await users->list(
    orm::query_wrapper<User>{}.eq("status", 1).order_by_desc("id"));
```

When MySQL and PostgreSQL intentionally use the same instance name, select the
provider explicitly while preserving the same repository type and operations:

```cpp
auto users = runtime.repository<User>(
    "primary", {}, application::database_provider::postgresql);
```

The provider-neutral path is:

```text
application_runtime::repository<T>()
  -> application_repository<T>
    -> repository<T>
      -> mapper<T>
        -> internal database_session
          -> managed session gateway
            -> MySQL/PostgreSQL pool
```

`mapper<T>` is the sole model-aware SQL boundary. It owns typed CRUD,
projections, paging, batch operations, native upsert selection and mapping.
`database_session` is an internal raw execution/transaction context and is not
re-exported by `cnetmod.orm`. Infrastructure tests or backend adapters that
need it must explicitly import `cnetmod.orm.database_session`.

Cross-model transactions obtain typed mappers from one unit of work; every
mapper shares the same leased connection and transaction:

```cpp
auto committed = co_await users->transaction<void>(
    [](auto& unit) -> task<std::expected<void, std::string>>
    {
        auto users = unit.template mapper<User>();
        auto saved_user = co_await users.insert(user);
        if (saved_user.is_err())
            co_return std::unexpected(saved_user.error_msg);

        auto orders = unit.template mapper<Order>();
        auto saved_order = co_await orders.insert(order);
        if (saved_order.is_err())
            co_return std::unexpected(saved_order.error_msg);
        co_return {};
    });
```

## Managed pool behavior

MySQL and PostgreSQL retain separate wire clients, pool implementations,
dialects, native diagnostics, cursors and upsert syntax. Those differences are
bound behind the Application repository factory. Pool sizing, TLS, deadlines,
health, recovery and shutdown remain configuration concerns rather than
business-code dependencies.

## UTC `DATETIME` conversion

`cnetmod.database.datetime` converts between Unix seconds and the timezone-free
`calendar_datetime` value used for UTC wall-clock database columns:

```cpp
auto value = cnetmod::database::datetime_from_unix_seconds(seconds);
auto seconds = cnetmod::database::unix_seconds_from_datetime(value);
```

Both directions return `std::optional` so invalid calendar components and years
outside the database representation are explicit. ORM parameter APIs accept
`calendar_datetime` and `std::optional<calendar_datetime>` directly through
`to_query_parameter`; do not add local `w_time`/`p_time` wrappers.
