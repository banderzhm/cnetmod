# MySQL

cnetmod 提供协程原生的 MySQL 协议客户端、预处理语句、连接池、TLS、健康检查和由 Application 托管的 ORM 适配器。

## 模块

```cpp
import std;
import cnetmod.protocol.mysql; // 协议客户端与连接池
import cnetmod.application;    // 托管服务与 Repository
import cnetmod.orm;            // 模型、条件构造器与结果契约
```

协议代码使用 `cnetmod::mysql`。与数据库厂商无关的持久化代码使用 `cnetmod::orm`；不存在 MySQL 专属的业务 Mapper 或 Repository API。

## Application ORM

```cpp
auto users = host->runtime().repository<user_record>("primary", {},
    cnetmod::application::database_provider::mysql);
if (!users)
    co_return;

auto result = co_await users->get_by_id(
    cnetmod::orm::param_value::from_int(42));
```

MySQL 托管适配层提供 Gateway、结果适配器、协议游标策略和原生 Upsert 语法；类型化 CRUD 与 XML 仍经过统一的 `repository<T> -> mapper<T>` 主链。

## 运行规则

- 从连接池租用客户端，不在单个协议客户端上并发复用操作。
- 使用预处理/绑定参数，禁止字符串插值 SQL。
- 一个事务从开始到提交或回滚必须持有同一租约。
- 协议响应未完整交付或传输失败后淘汰连接。
- 限制连接、池等待、查询和停机时间。
- 生产环境校验 TLS 并使用最小权限账号。
- 遥测中不记录 SQL 参数和凭据。

参见当前的 [ORM 指南](../advanced/orm-guide.md) 与 [事务指南](../mysql_transaction.md)。
