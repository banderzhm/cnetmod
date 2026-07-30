# MongoDB

cnetmod 提供协程原生的 MongoDB 连接，包含 OP_MSG、BSON 文档、hello 能力协商、认证、
TLS 配置、请求关联与有边界的消息校验。

## 连接与命令接口

```cpp
import cnetmod.protocol.mongodb;

mongodb::connection_options options;
options.host = config.host;
options.port = config.port;
options.database = config.database;
options.username = config.username;
options.password = config.password;
options.authentication_database = config.authentication_database;
options.connect_timeout = std::chrono::seconds{10};
options.tls = true;
options.tls_verify = true;
options.tls_ca_file = config.ca_file;

mongodb::connection connection(context);
auto connected = co_await connection.connect(options);
if (!connected) {
    logger::error("MongoDB 连接失败: {}", connected.error().message);
    co_return;
}

auto reply = co_await connection.command(
    options.database, mongodb::bson_document{{"ping", std::int32_t{1}}});
```

一个物理连接同一时刻最多执行一个命令。高并发服务必须使用数量受控的独立连接，并在连接
全部占用时实施背压；不能让多个请求处理器并发共享同一个 connection。

## BSON 所有权

`bson_document`、`bson_array`、`bson_value` 和 `bson_binary` 持有自己的数据。应用层应在
分配大缓冲区前限制文档大小；wire 层也会按本地配置和 hello 获取的服务端能力限制消息。
不得保存指向临时编码缓冲区的 view。

服务端命令错误也可能以 BSON 回复返回。除了检查 result 包装，还要检查回复中的 `ok`、
`code`、`codeName` 和 `errmsg`。未经清洗的数据库错误不能直接暴露给最终用户。

## 生产拓扑与高可用

`mongodb::connection` 表示单个物理连接；生产应用使用 `connection_pool` 或
`topology_connection_pool`。拓扑层实现副本集发现、SDAM 监控、读偏好和服务器选择，主节点
变化后会刷新拓扑并重新选择可写节点。连接池使用 FIFO 等待队列，支持等待超时、定向取消、
关闭唤醒、坏连接淘汰和空闲连接维护。

驱动同时提供 retryable read/write、逻辑会话、事务连接固定、未知提交结果重试、Change Stream
resume token 与断线自动恢复，以及 OP_COMPRESSED zlib/noop 协商。BSON wire 类型包含规范中的
常用和冷门类型。重试仍必须遵守 MongoDB 错误标签、事务语义和业务幂等性，不能把任意失败都
当成可安全重放。

## Spring Boot 服务迁移

`example_mongodb_production_service` 按配置、Repository、Service 和应用生命周期分层。
它对应 Spring Boot 中的 `@ConfigurationProperties`、Repository、`@Service`、健康检查和
`SmartLifecycle`，业务代码不直接管理 socket。副本集 seed、池上限、等待/命令超时、读偏好、
重试和 TLS 全部由环境配置注入；关闭时先停止接单，再排空在途命令和 Change Stream，最后关闭
拓扑池。

## 生产检查清单

- 使用 SCRAM-SHA-256 与最小权限数据库账号。
- 非可信本地环境必须启用 TLS 并校验服务端证书。
- 生产数据库端口优先保持私有；确需公网互操作测试时必须启用认证、最小权限、TLS 和来源限制。
- 限制池等待、连接、命令、响应大小和关闭时间。
- 每个响应都必须校验 request ID；帧格式、关联或传输错误后立即淘汰连接。
- 连接池必须提供背压，并暴露等待延迟、活跃/空闲连接数、命令延迟、错误标签和重连次数。
- 只有 MongoDB 错误标签与业务幂等性都确认安全时才能重试。
- 关闭时先停止接单并排空执行中的命令，再关闭连接。
- 日志不得包含凭证、完整 URI 或可能含个人信息的命令文档。

## 互操作测试

`testing/database/mongodb` 会让 cnetmod 与 pymongo 连接同一个真实服务。凭证通过
`CNETMOD_MONGODB_URI` 注入，原生驱动路径通过 `CNETMOD_MONGODB_DRIVER` 注入。完整命令见
`testing/database/README.md`。
