# ORM 指南

cnetmod 只有一条与数据库厂商无关的 ORM 主链：

```text
application_runtime::repository<T>()
  -> application_repository<T>
    -> repository<T>
      -> mapper<T>
        -> 内部 database_session
          -> session_gateway
            -> MySQL/PostgreSQL 连接池
```

应用代码使用 `application_repository<T>`。`mapper<T>` 是唯一感知模型的 SQL 边界，同时承载类型化 CRUD 和 MyBatis 风格 XML 语句。原始 `database_session` 只是内部执行与事务上下文，不是业务 CRUD 门面。

## 导入与模型

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

## 获取 Repository

```cpp
auto accounts = host->runtime().repository<account>("primary");
if (!accounts)
    return EXIT_FAILURE;
```

如果 MySQL 和 PostgreSQL 故意使用相同实例名，需要显式传入 `database_provider::mysql` 或 `database_provider::postgresql`。

## CRUD 与条件构造器

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

`get_one()` 使用严格基数语义：零行是成功的空结果，一行成功，多行返回 `std::errc::result_out_of_range`。读取数据前先检查 `model_result<T>::ok()`；原生错误码、SQLSTATE、框架错误、操作名和批次失败位置都会保留。

Repository 还提供投影、流式读取、批量写入、原生 Upsert 以及受保护的更新/删除。无条件更新或删除必须显式传入 `allow_full_table`。

## XML 语句

```cpp
cnetmod::orm::mapper_registry registry;
auto loaded = registry.load_directory("mapper");

cnetmod::orm::param_context parameters;
parameters.set("id", std::int64_t{42});

auto result = co_await accounts->get_one_xml(
    registry, "AccountMapper.findById", parameters);
```

XML 查询和写入与类型化 CRUD 经过同一条 Mapper/Repository 主链，共享连接租约、事务、自动策略、结果映射、方言归一化和遥测，不存在第二套 XML Repository。

## 跨模型事务

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

事务单元取得的所有 Mapper 共享一个连接和一个事务，不能在回调结束后继续持有。

## 当前 API 参考

完整且由源码校验的方法清单与配置语义维护在 [`skill/database/database-orm.md`](../../../skill/database/database-orm.md)。ORM 接口变化后必须运行 `python tools/check_orm_docs.py`。
