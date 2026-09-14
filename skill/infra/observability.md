# Observability 与 OpenTelemetry

> 通过一个 Telemetry Hub 低侵入地统一 Trace、Metric、Log、W3C 上下文传播和有界 OTLP/HTTP 导出。

**import**: `import cnetmod.observability;`

**源码**: `src/application/monitoring/telemetry.cppm`、`src/application/monitoring/otlp/otlp_http_exporter.cppm`、`src/application/monitoring/integration/http_client.cppm`

监控源码统一归属 `src/application/monitoring/`：`core/` 放协议无关的上下文、
操作结果和指标契约，`otlp/` 放编码与投递，`integration/` 放协议观测适配器。
目录归属不改变模块依赖方向：协议只依赖中立契约，不导入 Application Host 或 OTLP。
模块名保留 `cnetmod.instrumentation.*` / `cnetmod.observability.*`，避免目录整理
同时改变调用方 API。HTTP 关闭时 CMake 仍编译 `monitoring/core/`。

## 核心原则

1. 应用组合根只创建一个 `telemetry_hub`；协议与业务代码不依赖具体 OTEL SDK。
2. 协程上下文以值显式传递，禁止用 thread-local 保存活动 span。
3. Trace、Metric、Log 共用非阻塞提交、有限队列、批量、退避重试、丢弃计数和有界 flush。
4. 遥测失败不改变业务结果；队列满时只增加 dropped 计数。
5. 默认不记录 HTTP 正文、提示词、模型输出、工具参数、SQL 参数、凭据和消息载荷。

## 初始化

```cpp
cnetmod::net_init net;
auto io = cnetmod::make_io_context();
cnetmod::observability::telemetry_hub telemetry{*io, {
    .endpoint = "http://127.0.0.1:4318/v1/traces",
    .metrics_endpoint = "http://127.0.0.1:4318/v1/metrics",
    .logs_endpoint = "http://127.0.0.1:4318/v1/logs",
    .service_name = "orders",
    .service_version = "1.4.0",
    .resource_attributes = {{"service.owner", "platform"}},
    .headers = {{"Authorization", "Bearer token"}},
    // Optional: bridge completed framework logger events into OTLP logs.
    .capture_framework_logs = true,
}};
telemetry.set_sampling_ratio(0.25);
```

不设置 Trace endpoint 时导出器处于关闭状态，本地 OpenMetrics registry 仍可使用。只设置 Trace endpoint 时，Metric 和 Log endpoint 自动从同一 OTLP 基础地址派生。

## Trace

OpenAI 的 `telemetry_listener` 由独立模块 `cnetmod.observability.openai`
提供，使用时显式导入该模块。其公开命名空间仍为 `cnetmod::openai`。
`cnetmod.protocol.openai` 不再导出观测适配器或依赖 OTLP；协议保留通用
`run_listener` 事件接口，Application 在启用观测时装配适配器。
这是模块依赖隔离，不代表整个库已经按 OTEL 开关裁剪，也不是零性能损耗证明。

Application 的 OpenAI 监听器使用 `telemetry.measurements()`，将操作计数、活动数、
耗时、token、重试、拒绝和配置成本送入统一指标出口，由 Hub 执行本地聚合及 OTLP
投递。独立使用可构造 `telemetry_listener{telemetry.measurements(), telemetry.spans()}`。
原有接收 `metrics::registry&` 的构造方式仍只向该 registry 写指标；传入 span
exporter 不会使这些本地指标自动上报。不要同时把同一事件交给两种监听器，否则会重复计数。
内置 wire 回归通过本地 HTTP 接收器验证 Application 模型事件的 token 与耗时指标，
不代表已验证真实供应商请求或所有 GenAI 指标的端到端上报。

HTTP 服务端：

```cpp
server.use(cnetmod::http::tracing::tracing_middleware(
    telemetry.server_tracing()));
```

HTTP 客户端：

```cpp
cnetmod::observability::instrumented_http_client client{
    raw_client, telemetry.spans()};
auto response = co_await client.send(request, parent_context);
```

Redis 和 SQL API 接受 `trace_context + span_exporter`，gRPC metadata 自动注入/提取 `traceparent` 与 `tracestate`。OpenAI Agent 使用 `telemetry_listener` 记录 GenAI span、token、重试、耗时和估算成本；详细提示词与输出默认关闭。

### Kafka producer 装饰器

`import cnetmod.observability.kafka_producer;` 提供拥有原始 producer 的
`instrumented_kafka_producer`。`send(topic, record, parent, cancellation)` 的 parent
按调用显式提供；空 sink 直接返回原始发送任务。事务、flush、身份查询和 close
委托原始 producer，不改变事务提交边界。移动或析构前必须等待其任务结束。

Application 自动装配把 telemetry sink 注入 `kafka_service`；使用
`service.make_producer(options)` 获得带该 sink 的 producer，原始
`service.client().make_producer()` 仍是不带观测的协议入口。服务须比 producer
及其操作活得更久。当前工厂不自动监管 producer 的任务，也不自动进行停机 flush；
消费者处理范围、事务观测和消息指标仍是独立工作，不应理解为已全部自动接入。

## Metric

Prometheus/OpenMetrics 使用进程内 registry：

```cpp
telemetry.metrics().counter_add("orders_created_total");
router.get("/actuator/prometheus",
    cnetmod::metrics::openmetrics_handler(telemetry.metrics()));
```

需要推送到 OTLP 时提交结构化测量：

```cpp
(void)telemetry.submit_metric({
    .name = "queue.depth",
    .value = 12.0,
    .kind = cnetmod::observability::otel_metric_kind::gauge,
    .unit = "{message}",
    .attributes = {{"messaging.system", "kafka"}},
});
```

Application 框架会自动为服务启动、停止、恢复和健康探测产生本地指标及 OTLP 测量。

## Log

```cpp
(void)telemetry.submit_log({
    .severity = "WARN",
    .body = "dependency recovering",
    .trace_id = trace.trace_id,
    .span_id = trace.span_id,
    .attributes = {{"service.name", "redis"}},
});
```

HTTP access log 在 tracing middleware 启用时自动附加 `trace_id` 与 `span_id`。Application 生命周期日志同时作为 OTLP LogRecord 发送。不要把凭据或业务载荷放进 `body`/attributes。

普通 Logger 默认不注册 OTLP 回调，因而不引入观察者复制、OTLP 队列操作或
thread-local 查找。需要将框架日志导出时才设置 `capture_framework_logs: true`；回调运行在
Logger 的 worker 上，提交失败或队列满不会影响日志落盘。业务代码已经持有调用链上下文时，
使用显式值传递关联日志：

```cpp
import cnetmod.core.log;

logger::log(logger::level::info,
    {.trace_id = trace.trace_id, .span_id = trace.span_id},
    "order persisted");
```

Logger 把 ID 视为不透明字符串，不导入 OTEL，也不维护全局或 thread-local 的 active span；
未关联的 `logger::info` / `warn` 等既有调用路径保持不变。

## 上下文传播

- HTTP 使用 W3C `traceparent`/`tracestate`。
- gRPC 使用 metadata 中的相同字段。
- Kafka、AMQP 等支持头部的消息协议应在 producer 注入，在 consumer 提取；不支持头部的 Redis/SQL 只生成本地 CLIENT span。
- 传入可信边界前会校验 Trace Context，非法值不会继承。
- 消息载体复用时，注入会替换已有追踪头；新上下文的 `tracestate` 为空时删除旧值，避免沿用上一条调用链的供应商状态。
- 消息注入准备失败不会抛出异常或留下半更新的上下文。Kafka 和 MQTT 先准备追踪字段并预留容器空间，再以不抛异常的移动提交；AMQP 预备独立追踪节点，使用相同分配器及不抛异常的字符串比较转移节点。业务字段缓冲区和业务载荷不参与复制；开启传播时仍有追踪字段分配和容器操作成本，关闭路径不应调用注入适配器。

## 导出可靠性

- 每类信号使用有界 MPMC 队列；提交不等待网络。
- 单次 drain 按 `max_batch_size` 批量发送。
- 网络错误及 HTTP 429/502/503/504 指数退避，支持 `Retry-After`。
- 2xx 还必须通过 OTLP JSON acknowledgement 校验；不能只凭 HTTP 状态认为全部接收。
- `partialSuccess` 的拒收数量按 spans、log records、metric data points 分别统计，部分接收禁止整批重试；零拒收警告不扣减成功数。Collector 的诊断文本不进入日志或遥测。
- 导出器显式使用 HTTP/1.1、关闭重定向和 Cookie，网络接收阶段将响应体限制为 64 KiB，
  并限制 chunk framing。超大或非法 framing 会关闭连接并计入无效响应，不重试。
  原始 HTTP client 默认不启用这项额外限制；该选项不覆盖 HTTP/2、HTTP/3。
- 响应 SAX 解析另有限制：64 KiB、16 层容器；无效响应计入 `invalid_responses` 和
  `failed_batches`，不重试。64 KiB 是响应体上限，不是整个连接的总内存上限。
- `statistics()` 提供总 accepted/dropped/exported/retry/failed batch，并分别统计三类信号的 accepted/dropped。
- `exported` 表示 Collector 确认的 wire spans/log records/metric data points；累计指标快照可重复发送旧数据点，不能用 `accepted - exported` 推断丢失。拒收分别查看 `rejected_spans`、`rejected_metric_points`、`rejected_logs`；另有 `partial_batches`、`warning_batches`。
- 资源属性和鉴权头由三个 signal 共享，但不会出现在健康响应或普通日志中。

## 有序关闭

```cpp
// Run on the telemetry hub's owning io_context.
auto settled = co_await telemetry.shutdown(
    std::chrono::seconds{5}, std::chrono::milliseconds{500});
if (settled)
    io->stop();
else
    logger::error{"Telemetry shutdown has not settled: {}", settled.error().message()};
```

`flush()` 只等待已接收工作；超时不取消投递，也不关闭事件接收。`close()` 只停止接收，
不是后台任务 join。停机使用 `shutdown(delivery_timeout, cancellation_timeout)`：
先关闭接收并尝试排空，再取消未完成投递并等待收尾。成功表示任务已空闲、连接已关闭，
不表示每条记录都成功送达；仍需检查失败、拒收、丢弃统计。

`shutdown()` 返回错误时，不能仅因等待超时就停止或销毁事件循环。必须保持 hub 和
io_context 存活，继续处理完成通知并等待收尾；示例中的错误分支故意不调用 `stop()`。
它也不承诺强制终止不协作操作。Application Host 使用自己的整体停机预算安排收尾，
不能把示例的两段独立预算直接解释成 Host 的进程硬退出上限。

`try_settle_shutdown()` 供拥有事件循环的收尾协调器使用：在所属执行线程上取消投递，
不分配内存、不等待；返回 false 时必须继续驱动事件循环，返回 true 表示投递已空闲且
连接已关闭。调用是终态操作，不是普通空闲查询；不得用它检查仍在正常运行的 exporter。
Host 在最终连接收尾阶段也检查此状态，使用已有剩余预算，不因一次 shutdown 等待失败
就立即停止事件循环。这仍不保证预算耗尽后未协作资源的安全析构。
