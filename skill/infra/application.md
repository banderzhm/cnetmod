# Application 应用框架

> 将网络初始化、HTTP、路由、中间件、服务注册、生命周期、管理端点和 OpenTelemetry 组合成开箱即用的应用入口。

**import**: `import cnetmod.application;`

**源码**: `src/application/`

## 快速启动

```cpp
#include <cnetmod/config.hpp>

import std;
import cnetmod.application;

auto main() -> int
{
    auto result = cnetmod::application::run_application({
        .name = "orders",
        .http = {.port = 8080},
        .observability = {.otlp = {
            .endpoint = "http://127.0.0.1:4318/v1/traces",
        }},
    }, [](cnetmod::application::http_application& app)
    {
        app.routes().get("/hello", [](cnetmod::http::request_context& request)
            -> cnetmod::task<void>
        {
            request.json(cnetmod::http::status::ok,
                R"({"message":"hello"})");
            co_return;
        });
    });
    return result ? 0 : 1;
}
```

默认启用以下能力：

- `net_init` 与平台 `io_context` 生命周期管理。
- 异常恢复、停机期间拒绝新请求、请求 ID、HTTP 指标和安全的精简访问日志。
- `/actuator/health` 健康检查。
- `/actuator/prometheus` OpenMetrics 指标。
- 配置 OTLP endpoint 后自动启用 W3C Trace Context 与 OTLP Trace 导出。
- SIGINT/SIGTERM 和代码调用 `stop()` 使用同一条优雅停机路径。

## 数据库与消息系统自动装配

应用层为已启用的可选协议提供显式安装函数。安装只发生在配置阶段；组件会注册为应用单例，在 HTTP 接收请求前连接或启动，并在停机阶段按安装顺序的反方向释放。

```cpp
auto result = cnetmod::application::run_application(options,
    [](cnetmod::application::http_application& app)
    {
        auto& redis = cnetmod::application::install_redis(app, {
            .host = "redis.internal",
            .max_size = 32,
        });
        auto& mysql = cnetmod::application::install_mysql(app, {
            .host = "mysql.internal",
            .username = "orders",
            .database = "orders",
        });
        auto& kafka = cnetmod::application::install_kafka(app, {
            .bootstrap_servers = {{.host = "kafka.internal"}},
            .client_id = "orders",
        });

        app.services().emplace<order_service, default_order_service>(
            redis.pool(), mysql.pool(), kafka.client());
    });
```

| CMake 开关 | 安装函数 | 注册的应用服务 | 生命周期 |
|------------|----------|------------------|----------|
| `CNETMOD_ENABLE_REDIS` | `install_redis` | `redis_service` | 启动/取消连接池 |
| `CNETMOD_ENABLE_MYSQL` | `install_mysql` | `mysql_service` | 启动/取消连接池 |
| `CNETMOD_ENABLE_KAFKA` | `install_kafka` | `kafka_service` | 连接/关闭客户端门面 |
| `CNETMOD_ENABLE_MQTT` | `install_mqtt` | `mqtt_service` | 连接/断开客户端，保留重连策略 |
| `CNETMOD_ENABLE_AMQP091` | `install_amqp091` | `amqp091_service` | 连接、托管帧泵、取消并关闭 |
| `CNETMOD_ENABLE_AMQP10` | `install_amqp10` | `amqp10_service` | 连接/取消并关闭 |

没有启用对应 CMake 开关时，聚合模块不会导出该集成，因此不会引入无用的协议依赖。安装函数不会根据环境变量偷偷连接外部服务；是否启用始终由应用代码明确决定，连接参数可由应用自己的配置层注入。

## 外部配置

构造应用时自动读取以下环境变量并覆盖代码默认值：

| 环境变量 | 作用 |
|----------|------|
| `CNETMOD_APPLICATION_NAME` | 应用名 |
| `CNETMOD_HTTP_ADDRESS` | 监听地址 |
| `CNETMOD_HTTP_PORT` | 监听端口 |
| `OTEL_EXPORTER_OTLP_TRACES_ENDPOINT` | OTLP/HTTP Trace 完整地址 |
| `OTEL_EXPORTER_OTLP_ENDPOINT` | OTLP 基础地址，自动追加 `/v1/traces` |
| `OTEL_SERVICE_NAME` | OpenTelemetry service.name |
| `OTEL_SERVICE_VERSION` | OpenTelemetry service.version |
| `OTEL_RESOURCE_ATTRIBUTES` | 逗号分隔的资源属性 |
| `OTEL_EXPORTER_OTLP_HEADERS` | 逗号分隔的鉴权或租户请求头 |

## 类型安全服务注册

```cpp
app.services().emplace<user_repository, mysql_user_repository>(pool);
auto& users = app.services().require<user_repository>();
```

服务注册表在启动钩子成功后冻结，避免运行期间改变依赖图。它只负责应用拥有的单例对象，不做隐式构造、反射或全局 Service Locator。

## 生命周期

```cpp
app.lifecycle().on_start([&pool]()
    -> cnetmod::task<std::expected<void, std::error_code>>
{
    co_return co_await pool.connect();
});

app.lifecycle().on_stop([&pool]()
    -> cnetmod::task<std::expected<void, std::error_code>>
{
    co_return co_await pool.close();
});
```

启动钩子按注册顺序执行；停止钩子反向执行。停止阶段即使某个钩子失败，也会继续执行剩余清理，然后排空 HTTP 请求和 OTLP 队列。

## 设计边界

- `configuration` 只处理配置和值对象。
- `service_registry` 使用 Registry 模式管理应用单例，模板实现保留在 `.cppm` 中。
- `application_lifecycle` 负责异步启动和反向释放。
- `http_application` 是 Facade/Composition Root，不把协议细节泄漏到业务服务。
- `application/integration` 中每个外部系统使用独立 Adapter，将不同协议的启动/停止语义统一到应用生命周期。
- 业务模块继续依赖明确的构造函数参数；不要在业务代码中到处调用服务注册表。
