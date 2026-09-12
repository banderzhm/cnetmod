# Observability 与 OpenTelemetry

> 使用统一 Telemetry Hub 为 HTTP、OpenAI、Redis、SQL 和自定义组件提供低侵入的分布式追踪与指标。

**import**: `import cnetmod.observability;`

**源码**: `src/observability/telemetry.cppm`、`src/observability/otlp_http_exporter.cppm`、`src/observability/http_client.cppm`

## 核心原则

1. 应用组合根只创建一个 `telemetry_hub`，协议模块不直接依赖第三方 OpenTelemetry SDK。
2. 上下文必须作为值显式传递，禁止用线程局部变量保存协程链路上下文。
3. 遥测失败不得改变业务请求的结果；生产路径使用有界非阻塞队列。
4. 默认不记录提示词、模型输出、工具参数和 SQL 正文，敏感内容必须显式启用并限制长度。
5. 退出前先 `co_await telemetry.flush()`，再停止 `io_context`。

## 初始化

```cpp
import std;
import cnetmod.core;
import cnetmod.io;
import cnetmod.observability;

cnetmod::net_init net;
auto io = cnetmod::make_io_context();
cnetmod::observability::telemetry_hub telemetry{*io, {
    .endpoint = "http://127.0.0.1:4318/v1/traces",
    .service_name = "orders",
    .service_version = "1.4.0",
    .service_namespace = "commerce",
    .service_instance_id = "orders-01",
    .deployment_environment = "production",
    .resource_attributes = {{"service.owner", "platform"}},
    .headers = {{"Authorization", "Bearer token"}},
}};
```

`telemetry_hub` 是组合根（Facade + Adapter）：内部持有指标注册表和 OTLP exporter，对外只暴露稳定的 cnetmod 接口。将来替换 collector 或接入官方 SDK 时不需要修改协议和业务代码。

## HTTP 服务端

```cpp
router.use(cnetmod::http::tracing::tracing_middleware(
    telemetry.server_tracing()));
router.get("/metrics",
    cnetmod::metrics::openmetrics_handler(telemetry.metrics()));
```

中间件读取并校验 `traceparent`/`tracestate`，创建 SERVER span，并保留入站 `parentSpanId`。

## HTTP 客户端

```cpp
import cnetmod.observability.http;

cnetmod::http::client raw_client{*io};
cnetmod::observability::instrumented_http_client client{
    raw_client, telemetry.spans()};
auto result = co_await client.send(request, parent_context);
```

装饰器复制请求后注入 W3C 头，不改变原请求和底层客户端的所有权。

## OpenAI

```cpp
cnetmod::openai::telemetry_listener ai_telemetry{
    telemetry.metrics(), telemetry.spans()};
cnetmod::openai::run_config run{
    .run_id = request_id,
    .listeners = {&ai_telemetry},
    .trace_parent = inbound_context,
};
```

默认导出调用次数、并发量、耗时、token、重试、拒绝和估算成本；AI span 继承 HTTP trace，不捕获提示词和输出正文。

## Redis

```cpp
auto result = co_await redis.cmd({"GET", "key"}, parent_context,
    telemetry.spans());
```

## SQL / ORM

```cpp
auto result = co_await session.query("SELECT ...", parent_context,
    telemetry.spans());

// 只有完成隐私审查后才显式记录截断后的 SQL：
auto audited = co_await session.query("SELECT ...", parent_context,
    telemetry.spans(), {.capture_query_text = true, .max_query_bytes = 512});
```

## 导出可靠性

- 队列满时丢弃遥测，不阻塞业务线程，并累计 `dropped`。
- 网络错误及 HTTP 429/502/503/504 按指数退避重试。
- 支持 `Retry-After` 秒数。
- 2xx（包括 OTLP partial success）不重试，避免重复数据。
- resource、鉴权/租户请求头、真实起止时间、span kind、parent span 和 tracestate 均写入 OTLP。

## 有序关闭

```cpp
auto flushed = co_await telemetry.flush(std::chrono::seconds{5});
telemetry.close();
io->stop();
```

析构只停止接收新 span；需要尽量无损交付时必须在停止事件循环前显式 `flush()`。
