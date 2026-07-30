# PostgreSQL

cnetmod 提供协程原生的 PostgreSQL 客户端。MySQL 与 PostgreSQL 都通过独立的适配器接入
协议无关的 `cnetmod::orm`，两种协议不互相依赖。已有的 `CNETMOD_MODEL` 实体和 CRUD
服务迁移到 PostgreSQL 时，不需要再维护一套实体声明。

## 模块与文件

业务代码导入统一入口：

```cpp
import cnetmod.protocol.postgresql;
```

实现采用 `postgresql_connection.cppm`、`postgresql_connection.cpp` 这类见名知意的
接口/实现文件，不引入 `types.cppm` 之类职责不清的文件。统一模块导出连接配置、结果行、
预处理语句、客户端连接和 PostgreSQL ORM 适配层。

## 安全连接

一个 `postgresql::client` 对应一个物理连接，它不是隐式多路复用器；不能在同一个 client
上同时发起多个操作。生产服务应从连接池租用独立 client，每次只执行一个事务，并在会话
回到 `ReadyForQuery` 后才归还连接。

配置必须来自环境变量或密钥系统。生产环境应校验证书、限制连接超时，并使用最小权限账号；
不得把 URI 或密码写进源码。

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
    logger::error("PostgreSQL 连接失败: {}", connected.error_msg);
    co_return;
}
```

可以用 `server_parameters()`、`backend_process_id()` 和 `secure_channel()` 输出健康状态与诊断
信息，但日志中不得出现密码或完整连接 URI。

## 公共 ORM 与 MySQL 无缝迁移

PostgreSQL 使用 `cnetmod::orm::postgresql_session`。所有面向业务的 ORM API
统一位于 `cnetmod::orm`；`cnetmod::postgresql` 只保留连接、连接池、协议元数据和
方言适配器。实体元数据、`CNETMOD_MODEL`、结果映射、UUID 与雪花 ID 策略均与
具体数据库无关。

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

需要跨 MySQL/PostgreSQL 复用的查询不应包含数据库专属的引号、函数和 JSON 运算符。原生
SQL 仍然有方言差异；迁移时必须审查自增列、upsert、布尔转换、JSON 和标识符大小写规则。

## Spring Boot 服务迁移

`example_postgresql_production_service` 不是把所有逻辑塞进 `main` 或 `run` 的一次性 demo。
示例按照 Spring Boot 用户熟悉的职责拆分配置绑定、Repository、业务 Service、应用生命周期、
连接池和健康状态；业务事务始终固定在同一个池连接上。环境变量对应 `application.yml` 与
Secret，`service_application` 对应应用生命周期，`request_repository` 对应 Repository。

迁移时让 HTTP/消息处理器只调用 Service，不直接持有数据库连接。池满载会等待并实施背压；
瞬态建连失败使用有上限的重试和退避，事务结果未知时不得盲目重放。停机顺序为停止接单、排空
在途请求、回收池连接、停止 I/O 上下文。

## 预处理语句与事务

重复查询应使用预处理语句，参数必须独立于 SQL 文本，禁止字符串拼接。

```cpp
auto statement = co_await connection.prepare(
    "SELECT id, customer, total_cents FROM orders WHERE id = $1");
if (statement.is_err())
    co_return;

std::array parameters{postgresql::param_value::from_int(order_id)};
auto rows = co_await connection.execute(*statement, parameters);
```

从 `BEGIN` 到 `COMMIT`/`ROLLBACK` 必须持有同一个连接。取消或数据库错误导致事务失败时，
归还连接前必须回滚；传输层或协议错误后的连接应直接淘汰。

## 生产检查清单

- 非可信本地环境必须启用 TLS 并校验服务端证书。
- 为连接、池等待、查询和关闭设置上限，并向下传播取消。
- 按数据库容量而不是 HTTP 并发量配置连接池；连接用尽时实施背压。
- 使用参数化查询和最小权限账号。
- 归还前回滚脏连接，定期探活空闲连接，淘汰协议/传输错误连接。
- 记录延迟、池等待、SQLSTATE、重试和饱和度；不得记录可能含敏感信息的参数。
- 只重试确定幂等的操作，并使用有上限、带抖动的指数退避；绝不能盲目重放结果不明的提交。
- 关闭时先停止接单，再排空任务、终止空闲连接，最后停止 I/O 上下文。

## 互操作测试

`testing/database/postgresql` 会让 cnetmod 与 psycopg 连接同一个真实 PostgreSQL 服务，
对比连接、UTF-8、查询、并发和错误路径。凭证仅通过 `CNETMOD_POSTGRESQL_URI` 注入，
原生驱动路径通过 `CNETMOD_POSTGRESQL_DRIVER` 注入。完整命令见
`testing/database/README.md`。
