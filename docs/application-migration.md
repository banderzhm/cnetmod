# Application API 迁移指南

本次升级是有意的破坏式变更。旧的 `http_application`、`application_options`、`run_application()`、`application_lifecycle::on_start/on_stop` 和 `install_xxx()` 已删除，不提供双轨兼容层。

## 入口迁移

旧代码：

```cpp
auto result = cnetmod::application::run_application(options,
    [](cnetmod::application::http_application& app)
    {
        app.routes().get("/orders", handler);
    });
```

新代码：

```cpp
auto host = cnetmod::application::application_builder{"orders"}
    .configuration_file("application.json")
    .enable_auto_configuration()
    .routes([](cnetmod::http::router& routes)
    {
        routes.get("/orders", handler);
    })
    .build();

if (!host)
    return EXIT_FAILURE;
return host->run() ? EXIT_SUCCESS : EXIT_FAILURE;
```

## 生命周期迁移

不要再注册无身份的启动/停止回调。把资源封装为 `managed_service`，声明稳定的 `service_key`、required/optional、依赖关系、`start`、`stop` 和 `probe`。必须长期运行的循环通过 `service_context::supervisor` 启动。

这使框架可以在启动前检查依赖图、按拓扑层并发启动、精确回滚、逆序停止并把失败组件与阶段暴露给调用者。

## 集成迁移

旧 `install_redis(app, options)` 等安装函数改为 JSON 服务条目。只有同时满足以下条件才会装配：

1. 程序调用 `enable_auto_configuration()`；
2. 服务条目设置 `enabled: true`；
3. 构建时启用了相应 CMake 协议开关。

业务代码从冻结后的 registry 读取明确的具名接口：

```cpp
auto& cache = host.services().require<
    cnetmod::application::redis_service>("primary");
```

建议在组合根取得依赖后注入业务对象，不要在业务深层传递 registry。

## OpenAI 观测配置

手动使用 `cnetmod::openai::telemetry_listener` 时，新增显式导入：

```cpp
import cnetmod.protocol.openai;
import cnetmod.observability.openai;
```

监听器已经从协议分区移入独立观测模块；原始协议聚合入口不再导出它。
事件接口和监听器类名不变，无需给不使用遥测的 OpenAI 调用添加导入或配置。
Application 的 OpenAI 集成已显式接入该模块。

`openai_service::telemetry_listener()` 返回可空指针；关闭 Trace 和本地
Metrics 时不会构造监听器。推荐用 `run_configuration()` 合并调用配置：

```cpp
auto invocation = model_service.run_configuration(std::move(caller_configuration));
auto result = co_await model.invoke(request, invocation);
```

合并保留取消令牌、元数据和调用方监听器，重复合并不会重复注册同一个
服务监听器。配置中的监听器指针借用服务对象，其生命周期必须覆盖调用。
仅启用 Trace 时不聚合 GenAI 本地指标。

OpenAI 的事件 attributes 不再无条件导出为 OTEL 属性。自动导出仅包含
`input_tokens`、`output_tokens`、`total_tokens`（非负整数）、
`response_model`（有长度限制的字符串）和 `stream`（布尔值）。
metadata、tags、operation_id 和自定义业务字段仍传给调用方监听器，
但不会由 telemetry listener 自动导出。`capture_details` 仅允许捕获
有长度限制的请求/响应 detail，不会解除 attributes 白名单。

自定义运行观测可使用 `scope.succeed_lazy(factory)` 或
`scope.fail_lazy(factory, detail)`：factory 同步返回 JSON，仅在操作仍受
观测时执行一次。关闭监听器或已经结束时不执行；factory 异常不会改变
操作原本的成功/失败状态。detail 在调用期间借用，不跨异步边界保存视图。

开始事件使用 `run_scope::start_lazy(...)` 可跳过禁用观测时的 JSON 构造。
运行事件的操作标识现在位于 `run_event::operation_id`，不再由 scope
写入 `attributes["operation_id"]`；自定义监听器应读取独立字段。
telemetry listener 仍可识别手工事件中的旧属性，但优先使用独立字段。

`run_event::attributes` 默认是 null，只有实际需要属性时才构造对象。
`notify()` 在无需补充字段时同步借用原事件；监听器不得保留事件引用。
需要异步保存时由监听器显式复制。配置补充仍保留事件中的显式值，
可恢复的补充分配失败回退为交付原事件，不交付半合并的事件。

非作用域事件（例如重试、拒绝）使用 `config.notify_lazy(factory)`，
factory 同步返回 `run_event`。没有有效监听器时不会执行 factory；
可恢复的构造异常只丢弃该通知，不改变模型调用结果。模型治理与重试
实现已使用该入口，避免关闭观测时提前复制字符串或创建 JSON。

## HTTP 指标迁移

`tracing_middleware()` 未提供 `on_end` 时返回空中间件，
`server::use()` 会忽略空组件，不将其加入请求管道；禁用的
`server_metrics()` 同样适用。自定义管道若直接调用中间件，必须先
判断返回值是否非空。仅安装默认 tracing 不再隐式创建上下文或响应头。

Application 的自动 HTTP 指标统一通过 measurement adapter 写入本地
OpenMetrics 和已启用的 OTLP 出口，不再要求启用 tracing。原来的
`http_requests_total` 可改用 `http_server_request_duration_seconds_count`；
延迟直方图改为 `http_server_request_duration_seconds`。OTLP 名称为
`http.server.request.duration`，单位为秒。

默认不再用原始 URL path 做标签，避免业务 ID 造成高基数或泄漏。
本地标签 `method`、`status` 对应新名称 `http_request_method`、
`http_response_status_code`。独立使用旧 `openmetrics_middleware()` 的应用
不受自动装配迁移影响；不要同时安装两套 middleware 统计同一组请求。

## 停机迁移

把 `app.stop()` 改为 `host.request_stop()`。该方法可跨线程重复调用。框架会先撤销 readiness，再排空 HTTP、取消受监管任务、逆拓扑停止服务并 flush 三类 OTLP 信号。
# OTLP acknowledgement accounting

Application refreshes exporter statistics in its health loop for local
`/actuator/prometheus` scraping. New gauge families include
`otel_exporter_rejected_spans_total`, `otel_exporter_rejected_metric_points_total`,
`otel_exporter_rejected_logs_total`, `otel_exporter_worker_failures_total`,
`otel_exporter_partial_batches_total`, `otel_exporter_warning_batches_total`, and
`otel_exporter_invalid_responses_total`. Values are absolute snapshots, not
increments; refresh frequency follows the health-check interval. These metrics
are local-only to avoid recursively observing the exporter through its own queue.
Standalone hubs can call `refresh_exporter_metrics()` on their maintenance loop.

The exporter currently selects HTTP/1.1 (including HTTPS with HTTP/1.1 ALPN)
to enforce its 64 KiB response-body budget during reception. It disables cookies
and requests identity encoding; compressed responses are rejected, not inflated.
Ordinary clients keep their default protocol selection and leave the explicit
`client_options::http1_response_body_limit` disabled. That option does not apply
to HTTP/2 or HTTP/3. Unified protocol budgets and configurable exporter response
limits remain pending; do not treat the SAX parse bound as a universal transport
memory bound.

Collector success responses must contain valid OTLP JSON (normally `{}`), not
arbitrary text such as `accepted`. Partial acknowledgements are never retried.
Inspect `rejected_spans`, `rejected_metric_points`, `rejected_logs`,
`partial_batches`, `warning_batches`, and `invalid_responses` for delivery status.
Collector diagnostic text is intentionally not retained or logged.

`exported` counts acknowledged wire records: spans, log records, and metric data
points. It no longer counts producer metric submissions. Multiple measurements
can aggregate into one point, and cumulative snapshots can resend previous
points; do not infer loss from `accepted - exported`. `flush()` still means the
drain completed, not that every submitted record was accepted by the collector.

## MySQL sharded pool lifecycle

`mysql::sharded_connection_pool::async_run()` is now a lifetime task, not a
startup-only barrier. Run it concurrently with request processing under an owner
that awaits its completion. `request_stop()` and `cancel()` request shard shutdown;
the running `async_run()` joins dispatched shards and propagates failures. Keep
every shard event loop alive until that task completes. Do not immediately stop
event loops or destroy the pool after merely requesting cancellation.

This change joins shard maintenance, not outstanding borrowed leases. Borrowed
connections must still be returned before destroying the pool. Existing bare
`spawn(pool.async_run())` examples do not demonstrate supervised production
shutdown; their lifecycle owners still need migration. The MySQL skill's scoped
workload example now uses `when_all` and requests stop on both success and failure.

## OpenAI metric delivery

Application now supplies `telemetry_hub::measurements()` to its OpenAI listener.
This single sink performs local aggregation and OTLP metric submission according
to the hub settings. It preserves existing local metric names without recording
the same measurement twice. Span export remains independent.

Standalone integrations can use
`telemetry_listener{hub.measurements(), hub.spans(), options}`. The registry-based
constructor still records metrics only in its supplied registry; a span exporter
does not implicitly export that registry. Do not attach both listeners to the
same run. Metric sink failures must not replace model results or suppress spans.

## Real MySQL test

`test_application_mysql_live` is registered when HTTP and MySQL are enabled.
It connects only to `127.0.0.1:3306`, authenticates, performs three health PINGs,
and joins supervised shutdown without creating tables or changing records.
Set `CNETMOD_MYSQL_INTEGRATION=1` and provide `CNETMOD_MYSQL_TEST_USER`,
`CNETMOD_MYSQL_TEST_PASSWORD`, and `CNETMOD_MYSQL_TEST_DATABASE` for a disposable
test database. Run CTest with `-R '^test_application_mysql_live$'`.
Without the opt-in flag CTest marks it skipped (exit 77), not a live-test pass.
The Linux workflow provisions a MySQL 8.4 test service and enables this gate;
the workflow must actually run successfully before claiming interoperability.
