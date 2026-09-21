# PostgreSQL

cnetmod 提供协程原生的 PostgreSQL 客户端、预处理语句、连接池、TLS 和由 Application 托管的 ORM 适配器。

## 模块

```cpp
import std;
import cnetmod.protocol.postgresql;
import cnetmod.application;
import cnetmod.orm;
```

协议对象位于 `cnetmod::postgresql`。模型、条件构造器、Mapper 与 Repository 契约位于 `cnetmod::orm`。PostgreSQL 不再暴露第二套模型 CRUD 门面。

## Application ORM

```cpp
auto orders = host->runtime().repository<order_record>("primary", {},
    cnetmod::application::database_provider::postgresql);
if (!orders)
    co_return;

auto page = co_await orders->page(1, 50);
```

PostgreSQL 适配层提供 Gateway、结果映射、`$n` 占位符归一化和 `ON CONFLICT` Upsert 生成；类型化 CRUD 与 XML 共享统一的 Mapper/Repository 事务及策略链。

## 运行规则

- 一个物理客户端不是隐式多路复用边界。
- 从 `BEGIN` 到 `COMMIT` 或 `ROLLBACK` 保持同一个连接池租约。
- 仅在回到 `ReadyForQuery` 后归还连接，协议/传输失败后淘汰连接。
- 使用绑定参数、经过验证的 TLS、最小权限账号和有界超时。
- 只重试确定幂等的操作，绝不重放结果不明的提交。

参见当前的 [ORM 指南](../advanced/orm-guide.md)。
