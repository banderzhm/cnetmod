# Application 应用运行框架

> 提供配置、显式自动装配、依赖生命周期、任务监管、健康检查、OTEL 和优雅停机的生产级组合根。

**import**: `import cnetmod.application;`

**源码**: `src/application/`

## 核心原则

1. 使用 `application_builder` 构建，使用 `application_host` 运行；业务以 `application_module` 组合，不存在旧 `http_application` 兼容层。
2. 外部组件只有在配置中 `enabled: true` 且调用 `enable_auto_configuration()` 时才会装配。
3. 所有长生命周期组件实现 `managed_service`，关键后台协程交给 `task_supervisor`。
4. `build()` 完成严格配置校验、服务注册冻结和依赖环检查；失败时不启动网络监听。
5. required 组件启动失败会精确回滚；optional 组件进入降级恢复。运行期 required 恢复预算耗尽会请求应用停机。
6. HTTP handler 只读取 `health_registry` 的缓存，不同步探测 Redis、数据库或消息代理。

启动任务组使用整体启动截止时间，每个组件独立执行单组件截止时间。
optional 组件的单组件超时只触发降级与恢复，不能由共享计时器升级为整层失败；
整体启动预算耗尽或 required 组件失败仍触发回滚。真实 MySQL 适配器的本地
拒绝连接用例验证 optional 降级时 live=200、ready=503，并可正常主动停机。

## 最小入口

业务以 **模块**（`application_module`）组合进 Application。一个模块声明自己的配置节、注册
组件、贡献路由与中间件，并可挂接启动/停止钩子：

```cpp
#include <cnetmod/config.hpp>

import std;
import cnetmod.application;
import cnetmod.core.log;

namespace application = cnetmod::application;
namespace http = cnetmod::http;

struct order_options
{
    int page_size = 20;
};

class order_catalog
{
public:
    explicit order_catalog(const order_options& options) : page_size_(options.page_size) {}
    [[nodiscard]] auto list() const -> std::string
    {
        return std::format(R"({{"orders":[],"page_size":{}}})", page_size_);
    }

private:
    int page_size_;
};

class order_module final : public application::application_module
{
public:
    auto name() const -> std::string_view override { return "orders"; }

    void configure_options(application::options_registry& options) override
    {
        options.section<order_options>("orders");
    }

    auto register_components(application::registration_context& context)
        -> std::expected<void, std::string> override
    {
        context.components.singleton<order_catalog>(
            [](application::component_resolver& resolver) {
                return std::make_shared<order_catalog>(*resolver
                    .get<application::options_monitor<order_options>>("orders")
                    .current());
            });
        return {};
    }

    auto compose(application::composition_context& context)
        -> std::expected<void, std::string> override
    {
        auto& catalog = context.components.get<order_catalog>();
        context.routes.get("/orders",
            [&catalog](http::request_context& request) -> cnetmod::task<void> {
                request.json(http::status::ok, catalog.list());
                co_return;
            },
            http::endpoint_metadata{http::allow_anonymous{}});
        context.middleware.push_back(cnetmod::cors());
        return {};
    }
};

auto main() -> int
{
    auto host = application::application_builder{"order-service"}
        .configuration_file("application.yaml")
        .enable_auto_configuration()
        .add_module<order_module>()
        .build();
    if (!host)
    {
        logger::critical{"cannot start: {}", host.error().describe()};
        return EXIT_FAILURE;
    }
    return host->run() ? EXIT_SUCCESS : EXIT_FAILURE;
}
```

`application_host` 自己创建 `net_init`、`io_context`、CPU `thread_pool`、HTTP 服务、Telemetry Hub、健康缓存和任务监管器。`request_stop()` 可由其他线程重复调用，所有调用汇入同一条幂等停机路径。CPU 线程数通过 `application.cpu_threads` 配置，默认取硬件并发数且至少为 1；也可由 `CNETMOD_CPU_THREADS` 覆盖。该值运行时变更需要重启。

## 组合阶段

`build()` 按固定顺序执行命名阶段，任一阶段失败都返回 `build_error`，此时不监听端口、
不启动任何托管服务：

| 阶段 `build_phase` | 内容 |
|---|---|
| `configuration` | 读取 YAML/JSON、展开环境变量、应用 customizer、校验框架配置 |
| `options` | 模块 `configure_options()` 声明配置节；每个应用配置节必须被声明认领 |
| `registration` | `service_factory()`、自动装配、分片装配、模块 `register_components()` |
| `validation` | 托管服务依赖图校验 |
| `resolution` | 组件容器按依赖顺序急切构造全部单例 |
| `composition` | 模块 `compose()` 贡献路由与中间件 |

运行期还有两个钩子：`on_started()` 在托管服务启动完成、监听端口之前按注册顺序执行，
失败会回滚已启动的服务；`on_stopping()` 在 HTTP 排空、监管任务结束之后、托管服务关闭
之前逆序执行，只对已成功启动的模块调用一次。

`build_error{phase, component, path, message, code}` 的 `describe()` 输出形如
`resolution [orders_service] services.primary: component is not registered`。配置错误带
文档路径，例如 `http.sse.max_duration_ms: expected an integer number of milliseconds, got string`、
`services.cache.password: environment variable 'REDIS_PASSWORD' is not set and has no ${REDIS_PASSWORD:-default}`。
启用了未编译进当前构建的集成时，路径指向 `services.<name>.type`，错误码为 `not_supported`。

不需要独立类型的模块用 `make_module(name, module_hooks{...})` 以回调定义。模块名必须唯一，
重复或空模块在 `add_module()` 时抛出异常。

## 组件容器

`component_collection` 是模块注册组件的唯一入口，`component_container` 在 `resolution`
阶段急切构造所有单例；构造完成后容器只读，可并发查询。

```cpp
context.components.singleton<pricing_service>([](application::component_resolver& r) {
    return std::make_shared<pricing_service>(r.get<clock_source>("utc"));
});
context.components.instance(std::make_shared<clock_source>(), "utc");
context.components.alias<greeting, english_greeting>();   // 接口 → 实现
context.components.borrow(external_object, "external");  // 非拥有
```

- 工厂返回 `T`、`std::unique_ptr<U>` 或 `std::shared_ptr<U>`（`U` 为 `T` 或其派生类）。
- 空名字表示该类型的默认绑定；同一 `(类型, 名字)` 注册两次在 `resolution` 阶段报 `file_exists`。
- 缺失依赖报出完整解析链，例如 `component is not registered (required by pricing_service -> clock_source 'utc')`；
  循环依赖报 `dependency cycle: A -> B -> A`；工厂抛出的异常转为带组件名的诊断。
- 未注册的类型回退到托管服务注册表：自动装配的 `redis_service`、`mysql_service`、
  `postgresql_service`、`chat_model_service` 可以直接按类型和实例名注入。
- Host 预先注册 `application_runtime`、`execution_context`、`service_registry`、
  `observability::telemetry_hub` 与每个配置节的 `options_monitor<T>`（名字为配置节名）。
- 容器按创建顺序的逆序析构，组件总是先于它所借用的对象销毁；容器在 runtime、托管服务和
  模块之前析构。

组件在构建期就绪，因此 `compose()` 中可以按引用捕获组件，不需要 `shared_ptr` 或延迟绑定。

## 应用配置节（Options）

框架配置节（`application`、`logging`、`http`、`management`、`observability`、
`crash_dump`、`lifecycle`、`health`、`orm`、`security`、`services`）严格校验未知键。
其他顶层配置节属于应用，必须由某个模块声明：

```cpp
options.section<llm_options>("llm", /*required=*/true)
    .validate([](const llm_options& value) -> std::expected<void, std::string> {
        return value.providers.empty()
            ? std::unexpected(std::string{"at least one provider is required"})
            : std::expected<void, std::string>{};
    })
    .reload(application::options_reload::runtime_safe);
```

- `T` 为可默认构造、可由 cnetmod.json（Glaze）映射的聚合类型；缺省的键取成员默认值，
  嵌套映射逐层合并；未知键报出完整路径（空对象成员视为 map，接受任意键）。
- 未被任何模块声明的应用配置节使 `build()` 在 `options` 阶段失败。
- 注册阶段按配置决定注册什么（例如每个供应商一个组件）时，用
  `context.options.current<T>("name")` 读取已绑定的快照；类型不符或未声明时构建在
  `registration` 阶段失败。需要感知重载的组件仍应注入 `options_monitor<T>`。
- 组件通过 `options_monitor<T>`（名为配置节名）读取：`current()` 返回线程安全的不可变快照，
  `on_change()` 返回 RAII `options_subscription`。
- `reload_configuration()` 先校验所有变更的配置节，全部通过后才发布；`runtime_safe`
  配置节原子替换快照并通知订阅者，`restart_required`（默认）只把结果标记为需要重启。

环境变量引用支持 `${NAME}`、`${NAME:-default}` 与转义 `$${`；空值视为未设置。无默认值
的缺失变量是带路径的配置错误。`expand_environment_references()` 公开同一规则。

## 执行与 Runtime

`application_runtime` 只暴露与集成无关的执行能力：`spawn_managed()`、`offload()`、
`schedule_on_cpu()`/`resume_to_event_loop()`、`files()`、`rest()`、`json()`、
`compression()`、`cancellation()`、`tasks()`、`telemetry()` 与 `configuration()`。
仓储、Redis、Chat Model 等集成以组件方式注入，新增集成不会修改 runtime 接口。

`runtime.executor()` 返回 `execution_context`：非拥有地提供 Host 事件循环与 CPU 池，
供需要执行上下文的协议层组件使用（计时器、`ai::resilient_chat_model` 等装饰器、显式
超时 `with_timeout()`、`sleep()`）。调用方不得 run、stop 或 restart 该事件循环。JWT
签发与验签直接调用 `security::sign_jwt/verify_jwt(executor.cpu_pool(), executor.event_loop(), ...)`。

Route 中优先使用 `offload()` 包装一段纯 CPU callable：它会在 Application CPU 池运行，
无论正常返回还是抛异常，等待方都会恢复到当前 Application 事件循环。只有算法必须跨
多个异步步骤持续驻留 CPU 池时才成对使用 `schedule_on_cpu()` 与
`resume_to_event_loop()`；切回事件循环前不得读写 `request_context`：

```cpp
auto compose(application::composition_context& context)
    -> std::expected<void, std::string> override
{
    auto& runtime = context.runtime;
    context.routes.post("/score", [&runtime](http::request_context& request)
        -> task<void> {
        auto input = std::string{request.body()};
        auto score = co_await runtime.offload(
            [input = std::move(input)] { return calculate_score(input); });
        request.text(http::status::ok, std::to_string(score));
    });
    return {};
}
```

`parse_offloaded(runtime, text)` 与 `dump_offloaded(runtime, value)` 在 Host CPU 池执行
JSON 解析和序列化，避免 route 协程阻塞事件循环。两者拥有输入直到执行完成并返回
`std::expected`；语法错误为 `invalid_argument`，内存不足保持 `not_enough_memory`。

## HTTP 先响应、后台继续执行

该场景使用 `application_runtime::spawn_managed()`，不能直接 `spawn()` 一个失管协程。
注册动作同步完成，因此 handler 可以在任务被 supervisor 接管后立即返回 HTTP 202；
Application 停机时会取消并等待任务，不会让协程访问已经析构的服务。

```cpp
auto compose(application::composition_context& context)
    -> std::expected<void, std::string> override
{
    auto& runtime = context.runtime;
    auto reports = context.components.shared<report_service>();
    context.routes.post("/reports", [&runtime, reports](http::request_context& request)
        -> task<void> {
        // request_context 只活到本次请求结束；后台任务必须按值拥有所需输入。
        auto input = std::string{request.body()};
        auto job_id = make_job_id();
        recovery_policy one_shot;
        one_shot.budget = std::chrono::milliseconds{0};
        auto accepted = runtime.spawn_managed(std::format("report:{}", job_id),
            [reports, input = std::move(input), job_id](cancel_token& cancellation)
                -> task<std::expected<void, std::error_code>> {
                co_return co_await reports->generate(job_id, input, cancellation);
            },
            one_shot,
            false); // 单个业务任务失败不触发整个应用停机
        if (!accepted) {
            request.json(http::status::service_unavailable,
                R"({"error":"background task was not accepted"})");
            co_return;
        }
        request.json(http::status::accepted, make_job_accepted_document(job_id));
    });
    return {};
}
```

必须遵守以下生命周期语义：

1. 后台 lambda 不捕获 `request_context&`、请求 body 的 view、局部变量引用或裸业务指针。
2. 输入按值移动，服务使用 `shared_ptr` 或其他覆盖任务生命周期的受管理所有权
   （`components.shared<T>()` 与容器共享所有权）。
3. `202 Accepted` 只表示 supervisor 已接管，不表示任务成功；任务状态应持久化，并提供
   `GET /jobs/{id}` 等查询接口。
4. 任务名称必须唯一。重复名称返回 `errc::file_exists`，停机期间注册返回
   `errc::operation_canceled`。
5. 非幂等任务把 recovery budget 设为 0；需要自动恢复的任务必须先设计幂等键，再配置
   有界退避和恢复预算。
6. `required=false` 适合单个用户任务；基础设施泵、关键消费循环等应使用 required 任务，
   恢复预算耗尽后由 Application 撤销 readiness 并进入优雅停机。

`application_runtime::files()` 返回 `async_file_template`，其
`open/read/write/flush/close/stat/read_all/write_all/remove` 内部使用 Host 的事件循环，业务和
领域端口无需传递 `io_context&`。原子替换文件时使用
`write_all(path, content, file_write_durability::flushed)` 在关闭前刷盘，再做同目录改名。
涉及请求超时的调用应使用带独立 `cancel_token&` 的重载；`remove()` 对不存在的目标幂等成功。

`application_runtime::rest()` 返回 `rest_template`，用于业务出站 HTTP 调用。它组合既有
HTTP client pool 与 OTEL instrumented client，统一提供 `exchange/get/post/put/patch/remove`；
成功请求的 client 才会归还复用池，传输失败或取消的 client 会关闭并丢弃。业务代码不得为
普通 HTTP 调用自行创建 `http::client`，也不得为了完成一次请求手工停止事件循环。需要请求头
或其他高级选项时构造 `http::request` 后调用 `exchange()`；需要操作级取消时使用接收
`cancel_token&` 的重载。`rest_template_options::default_headers` 注入模板级请求头，
`rest_request_options::headers` 注入单次请求头；名称按 HTTP 规则忽略大小写，单次值覆盖默认值。
Template 的接口与实现统一位于 `src/application/template/`，不在 Application 根目录堆放实现。

## 路由策略与端点元数据

路由的访问策略在注册处用 `http::endpoint_metadata` 声明，中间件在路由匹配后通过
`request_context::endpoint()` 读取，不再维护与路由分离的路径白名单：

| 元数据 | 含义 |
|---|---|
| `http::allow_anonymous` | 不解析凭据，`jwt_auth` 与 `authorize` 都直接放行 |
| `http::optional_authentication` | 无凭据按匿名继续；携带的凭据必须有效（默认） |
| `http::required_permissions{all_of, any_of}` | `authorize` 的默认权限要求，段支持 `*` |
| `http::endpoint_name{"orders.list"}` | 稳定操作名，用于日志、指标与 API 文档 |

未声明认证元数据的路由必须认证。策略按方法区分：同一路径的 `GET` 可以匿名而 `DELETE`
需要权限。未匹配任何路由的请求默认直接交给路由器返回 404（`jwt_auth_options::unmatched`、
`authorization_options::authorize_unmatched` 可改为强制认证）。`router::endpoints()` 列出
全部端点，可用于生成文档或在启动时审计策略。

## 数据访问

仓储是组件。模块用 `add_repository<T>()` 注册，按类型注入：

```cpp
application::add_repository<order_record>(context.components,
    {.instance = "primary",
     .policies = {.logical_delete_policy = cnetmod::orm::logical_delete_config{
         .field_name = "deleted_at",
         .mode = cnetmod::orm::logical_delete_mode::nullable_datetime}}});

auto& orders = context.components.get<application::managed_repository<order_record>>();
auto& factory = context.components.get<application::repository_factory<order_record>>();
auto scoped = factory.for_request(request);   // 绑定请求的租户与数据权限快照
```

`repository_factory<T>` 在构建期解析数据源，缺失或同名歧义的数据源让 `build()` 在
`resolution` 阶段失败。`shared()` 返回进程级仓储；`for_request()` 返回绑定请求
`tenant_scope` 与 `data_permission_scope` 快照的仓储，认证未绑定数据权限时应用空范围
（对 `DATA_PARTITION`/`DATA_OWNER` 模型拒绝全部行）。每个仓储只构建一次拦截链并在所有
操作间复用。严格 SaaS 模式由配置 `orm.tenant_scope_required: true` 开启并在构建时冻结：
租户模型没有请求租户快照时返回 `permission_denied`，且不提供进程级 `shared()` 仓储。

## Chat Model

启用 Chat Model provider 自动装配后，`chat_model_service` 按实例名注入；
`make_template(options)` 返回的 `chat_model_template` 实现 `ai::chat_model`。多实例（多
key、多供应商）用 `add_chat_model()` 组合成一个 `ai::chat_model` 组件：

```cpp
application::add_chat_model(context.components, "assistant",
    {.instances = {"deepseek-key-1", "deepseek-key-2"},
     .routing = application::chat_model_routing::round_robin,
     .resilience = cnetmod::ai::resilient_model_options{.max_attempts_per_model = 2},
     .governance = cnetmod::ai::governed_model_options{.max_concurrency = 16},
     .template_options = {.request = {.model = "deepseek-chat"}}});

auto& model = context.components.get<cnetmod::ai::chat_model>("assistant");
```

由内到外依次为：每个实例重试后按声明顺序故障转移到其余实例（首个流式分片交付后不再重试，
避免重复或拼接输出）；`round_robin` 按调用轮换起始实例，`ordered` 总从第一个实例开始；
`governance` 施加并发隔离、熔断与限流。任一实例缺失都会让 `build()` 在 `resolution` 阶段失败。

每个 managed provider 拥有固定容量连接池，一次 invoke/stream 独占一个 lease 到终态，避免
keep-alive 响应交错。文本重载复制 `chat_model_template_options::request`，按 system、默认
消息、历史、当前 user 输入的顺序构造请求；显式 `chat_request` 重载不改写调用方消息。

运行时端点、凭据或池容量变更通过 `chat_model_service::reconfigure(configuration, cancellation)`
提交完整 provider 配置。具体 provider 先校验配置并建立整代新连接，全部成功后才原子发布；
失败时旧代保持服务。已借出的 model lease 持有旧客户端所有权，会在请求结束后自然退休，
新请求只进入新代。不要把 `chat_model_pool::reset()` 暴露给业务代码，否则会绕过 provider
校验、连接建立、Telemetry 和生命周期边界。OpenAI 配置支持热更 `base_url`、`api_key`、
`tls_verify`、`timeout_seconds` 与 `pool_size`，传入属性是完整替换而不是局部 patch。

```cpp
application::chat_model_reconfiguration next;
next.properties = {
    {"base_url", "https://new-gateway.example/v1"},
    {"api_key", rotated_key},
    {"tls_verify", true},
    {"timeout_seconds", 30},
    {"pool_size", 8},
};
auto& service = components.get<application::chat_model_service>("assistant");
auto reloaded = co_await service.reconfigure(std::move(next), &cancellation);
```

`chat_model_template::conversation(session_id, store)` 提供显式会话边界。
同一 session 的调用通过共享协程门串行化，不同 session 可占用不同池连接并行运行；
持久层仍是唯一真相。成功回合才用 `append_batch(user, assistant)` 原子追加，失败或取消
不产生半回合。`history_limit` 控制每次读取的最近消息数，零表示读取全部。

## SSE

Application 的 SSE 接口在模块 `compose()` 中用 `router::sse_get()` 或
`router::sse_post()` 声明。框架按请求注入 `sse_stream&`，业务只序列化事件 payload；
SSE 响应头、具名帧编码、心跳、断线返回值和终止帧由框架负责。

如果同一路由按请求参数在普通 JSON 与 SSE 间切换，保持普通 `get/post` 路由，
在完成参数校验和资源检查、确认要流式响应后调用
`request_context::with_sse(handler, options)`；把整个模型调用与事件写出放进
handler 内，业务自己的事件 writer 也应在其中构造和使用。只调用
`sse_begin()` / `sse_send()` 虽仍有默认 5 秒单次写超时，**没有**
`max_duration` 总时长看门狗；普通 `request_timeout` 也只是软检测。
`with_sse()` 使用传入的 `sse_stream_options`（省略时为 120 秒／5 秒）；
不要假定 Application 为独立 SSE 路由注入的 `http.sse` 配置会自动应用到动态入口。

```cpp
context.routes.sse_post("/chat", [](http::request_context& request,
                                    http::sse_stream& stream) -> task<void> {
    if (!co_await stream.send(R"({"text":"hello"})", "delta"))
        co_return;
    co_await stream.send(R"({"tokens":1})", "done");
    co_await stream.finish();
}, http::sse_stream_options{
    .max_duration = std::chrono::seconds{60},
    .write_timeout = std::chrono::seconds{3},
}, http::endpoint_metadata{http::optional_authentication{}});
```

`stream.started()` 为 false 时，业务仍可返回普通 HTTP 错误；一旦为 true，只能继续写 SSE
错误/结束帧或结束连接。Application recover 中间件对未捕获异常遵守该边界，不会在 SSE
已经提交后错误地回退 JSON 响应。

Application 默认从 `http.sse` 为所有 SSE 路由注入超时：整条流最长 120 秒，单次响应头或
事件帧写出最长 5 秒。路由的 `sse_stream_options` 可按接口缩短或延长，但两个值都必须
为正数。达到总时限或写超时后，当前写操作会被取消、流进入 `failed`、socket 被关闭；不会
继续占用连接或回退普通 HTTP。调用 OpenAI、数据库等下游操作时应通过
`request.with_deadline()` 继承同一个总预算，避免业务生产者在连接结束后继续运行。

## 基础设施扩展与中间件顺序

自定义基础设施使用 `application_builder::service_factory()` 在 `registration` 阶段创建。
该 API 是协议适配器的基础设施扩展点，不是业务执行入口；业务功能使用模块。工厂通过
`application_service_context` 获得 host 所有的 `io_context`、Telemetry Hub、
`task_supervisor` 和只读配置，返回一个 `managed_service`。工厂错误、空服务或重复
服务身份都会让 `build()` 失败；所有阶段完成后 registry 才冻结。模块需要登记托管服务时
使用 `registration_context::manage()`。

业务 HTTP 中间件由模块在 `compose()` 中追加到 `composition_context::middleware`，
按模块注册顺序、模块内追加顺序执行，不影响独立的管理端点。执行顺序固定为：框架异常恢复、
停机跟踪、请求 ID、追踪、指标和请求超时位于业务中间件外层，访问日志位于业务中间件内层。
路由在中间件之前完成匹配，因此业务中间件可以读取 `request_context::endpoint()`。空中间件
使 `build()` 在 `composition` 阶段失败。

host 显式持有预先创建的顶层编排协程，以协程帧内队列节点进入事件循环，不使用 detached 包装派发该主任务。编排异常进入清理边界；`run()` 在事件循环返回后检查主任务已结束。

编排异常的清理入口停止监听后，立即取消被跟踪请求并中止业务和管理连接的 socket I/O，
进入收尾并先等待被跟踪 handler 退出，再取消、等待监管任务，然后关闭服务；不等待再次
走正常请求排空阶段，也不覆盖最早的原始错误。收尾本身失败的紧急回退仍请求监管任务停止。
故障注入测试覆盖活跃 HTTP handler 等待期间的编排分配失败、请求取消和原始错误保留。
该测试同时挂载受管理依赖及后台任务，检查 handler 异步完成取消收尾后才通知后台任务停止，
依赖只启动、关闭一次。
这不等于可强制终止未响应取消的业务协程，也不覆盖任意多层依赖和后台任务的全部竞争。

业务及管理 HTTP 接收循环均注册为必需的受监管任务，监听异常不自动重启，而是进入统一停机路径。host 在接收事件循环上调用 `server::stop()`，清理阶段等待 supervisor；尚未开始执行就被取消的监听任务不会重新启动监听。监听及健康任务注册失败均进入回滚，不允许静默忽略。已接受的连接仍有独立生命周期，不能由接收循环已结束推断连接已排空。

## 配置

优先级固定为：框架默认值 < YAML/JSON < 环境变量 < builder `configure()` 显式覆盖。

安全组件必须从已经解析并校验的 Application 配置读取凭据。中间件和业务
代码不得直接调用 `std::getenv()`；这样未知字段、缺失值、长度约束、集中脱敏
与重载分类会在启动前统一完成。JWT 使用 `security.jwt`：

```json
{
  "security": {
    "jwt": {
      "enabled": true,
      "issuer": "order-service",
      "secret": "${ORDER_SERVICE_JWT_SECRET}",
      "expires_in_seconds": 604800,
      "session_idle_seconds": 900
    }
  }
}
```

`enabled: true` 时，issuer 必须非空、secret 至少 32 字节，
`expires_in_seconds` 在 1 到 2592000 之间，`session_idle_seconds` 在 1 到
`expires_in_seconds` 之间。两者分别供 JWT 的绝对有效期和应用会话空闲超时使用。
`${...}` 只由
配置加载器集中展开；Application 代码通过
`runtime.configuration().security.jwt` 获取经过校验的只读值。JWT 配置变更
被分类为需要重启，且 `secret` 在配置诊断输出中始终脱敏。

新项目推荐使用 YAML（`.yaml` 或 `.yml`）作为 Application 配置格式；JSON 继续作为
兼容输入格式，并仍用于 HTTP/消息负载的类型化编解码，两者不是同一层职责。

`application_builder::configuration_file()` 根据 `.json`、`.yaml` 或 `.yml`
扩展名选择解析器。YAML 由独立的 `yaml_cpp` C++23 Modules 门面和
`cnetmod.application.yaml_configuration` 适配器转换为同一个 JSON 文档模型；因此
两种格式有完全相同的字段校验、`${ENV_VAR}` 注入、脱敏及热重载语义。YAML 映射键
必须为字符串且不得重复，文档根必须为映射；锚点展开限制为 64 层以拒绝循环或病态输入。
解析器与模块门面分别以固定提交收录在 `3rdparty/yaml-cpp` 和
`3rdparty/yaml-cpp-modules` Git submodule 中。cnetmod 构建不会再通过
`FetchContent` 隐式下载 YAML 依赖；克隆后应执行
`git submodule update --init --recursive`。开发时可分别用
`CNETMOD_YAML_CPP_SOURCE_DIR` 和 `CNETMOD_YAML_CPP_MODULES_SOURCE_DIR`
指向本地版本。

下面的 JSON 仅用于展示与 YAML 等价的完整兼容字段；可直接使用仓库中的
`examples/application/application.yaml` 作为推荐配置模板。

```json
{
  "application": {
    "name": "order-service",
    "install_signal_handlers": true,
    "cpu_threads": 4
  },
  "crash_dump": {
    "directory": "crash"
  },
  "logging": {
    "level": "info",
    "format": "json"
  },
  "http": {
    "address": "0.0.0.0",
    "port": 8080,
    "request_timeout_ms": 30000,
    "sse": {
      "max_duration_ms": 120000,
      "write_timeout_ms": 5000
    }
  },
  "management": {
    "enabled": true,
    "address": "127.0.0.1",
    "port": 8081,
    "same_port": false
  },
  "observability": {
    "tracing": true,
    "metrics": true,
    "logs": true,
    "sampling_ratio": 1.0,
    "otlp": {
      "traces_endpoint": "http://127.0.0.1:4318/v1/traces",
      "metrics_endpoint": "http://127.0.0.1:4318/v1/metrics",
      "logs_endpoint": "http://127.0.0.1:4318/v1/logs"
    }
  },
  "orm": {
    "sharding": {
      "enabled": false,
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
  },
  "services": {
    "primary-cache": {
      "type": "redis",
      "instance": "primary",
      "enabled": true,
      "required": true,
      "host": "redis.internal",
      "password": "${REDIS_PASSWORD}",
      "recovery": {
        "initial_delay_ms": 500,
        "maximum_delay_ms": 30000,
        "budget_ms": 120000,
        "multiplier": 2.0,
        "jitter": 0.2
      }
    }
  }
}
```

`orm.sharding.enabled` 默认关闭，因此普通 ORM 和现有单库配置不受影响。开启后，每个
`topologies` 条目会注册一个同名 `mysql_sharded_session_gateway`；`databases` 必须引用
已经启用的具名 MySQL 服务。启动前会验证表标识符、分表数量、重复数据库实例和服务引用。
全分片读取只能通过显式 `scatter_read()` / `scatter_gather()` 发起；跨库写事务只能通过
显式 `distributed_transaction()` 发起，多库路径使用 MySQL XA 两阶段提交。

服务条目的对象键只是配置绑定名；`type + instance` 才是服务身份，因此同一接口可配置多个具名实例。未知框架字段、未知集成属性、非法端口、非法超时、重复服务身份和缺失的必要凭据都会使 `build()` 失败。

HTTP client 的 `connect_timeout_ms`、`request_timeout_ms` 和 OpenAI 的
`timeout_seconds` 在创建对应服务前检查整数类型及正数范围，不接受布尔、小数、
字符串、null、零、负数或超范围整数，避免 JSON 转换产生截断或回绕。
缺省字段仍使用原有默认值。此校验不表示其他所有集成参数均已完成边界审计。

Redis、MySQL、PostgreSQL、MongoDB、Kafka、MQTT、AMQP 0-9-1 和 AMQP 1.0
的独立 `port` 属性同样在转换前检查，必须为 1～65535 的整数；缺省保持协议默认端口。
不能依赖无符号转换后的端口值做合法性判断，否则 65537 等值可能回绕为另一个端口。

MySQL 服务还支持 `ssl`（`disable`、`enable`、`require`）、`tls_verify`、
`tls_ca_file`，以及 `connect_timeout_ms`、`pool_timeout_ms`、
`retry_interval_ms`、`ping_interval_ms`、`ping_timeout_ms`。所有显式超时必须是
1～86400000 毫秒的整数；未知 TLS 模式在 `build()` 阶段拒绝。

四类数据库连接池的 `minimum_size` / `maximum_size` 使用共享校验：前者可为零，
后者必须为正整数，按缺省值补齐后仍必须满足 minimum ≤ maximum。转换前拒绝
错误 JSON 类型、负数和超出可表示范围的数值，不静默截断或调整用户配置。
该检查不承诺机器有足够内存或数据库连接配额，实际资源获取仍在生命周期内处理。

采样率必须是 [0, 1] 的有限值，builder 提供的 NaN/无穷值同样拒绝。HTTP 请求超时
未设置时保持原语义；显式设置则必须为正数。框架的正时长参数以及 HTTP client 超时
不超过 steady_clock 可表示时长的一半，与恢复策略的保守上限一致；这不是对所有
运行期截止时间运算的溢出安全证明。

`${ENV_VAR}` 在解析阶段展开。`password`、`secret`、`token`、`api_key`、凭据和连接串会由 `redact_configuration()` 脱敏；框架不把请求正文、提示词、SQL 参数、凭据或消息载荷写入健康响应和遥测。

每个 `application_host` 默认在构建完成后、打开监听端口前安装进程级崩溃转存。Windows
生成 `.dmp` 和文本报告；Unix 开启系统 core dump 并写入最小崩溃记录。该安全网不依赖
日志或 OTEL，二者关闭、阻塞或故障时仍保持工作。`crash_dump.directory` 只允许在下次
进程启动时变更，因此运行时重载会报告需要重启。

支持的环境变量包括：

- `CNETMOD_APPLICATION_NAME`、`CNETMOD_HTTP_ADDRESS`、`CNETMOD_HTTP_PORT`、`CNETMOD_LOG_LEVEL`
- `OTEL_EXPORTER_OTLP_ENDPOINT`、`OTEL_EXPORTER_OTLP_TRACES_ENDPOINT`
- `OTEL_EXPORTER_OTLP_METRICS_ENDPOINT`、`OTEL_EXPORTER_OTLP_LOGS_ENDPOINT`
- `OTEL_SERVICE_NAME`、`OTEL_SERVICE_VERSION`
- `OTEL_RESOURCE_ATTRIBUTES`、`OTEL_EXPORTER_OTLP_HEADERS`

## managed_service

```cpp
class order_worker final : public cnetmod::application::managed_service
{
public:
    auto key() const -> cnetmod::application::service_key override;
    auto dependencies() const
        -> std::vector<cnetmod::application::service_key> override;
    auto requirement() const noexcept
        -> cnetmod::application::service_requirement override;
    auto start(cnetmod::application::service_context& context)
        -> cnetmod::task<std::expected<void, std::error_code>> override;
    auto stop(cnetmod::application::service_context& context)
        -> cnetmod::task<std::expected<void, std::error_code>> override;
    auto probe(cnetmod::application::service_context& context)
        -> cnetmod::task<cnetmod::application::health_report> override;
};
```

`service_context` 提供 `io_context`、Telemetry Hub、Task Supervisor、取消令牌和本阶段截止时间。实现必须响应取消和截止时间，禁止在协程中同步阻塞。

`managed_service::cleanup_required() const noexcept` 默认返回 false。若组件启动失败
或抛异常后仍持有未完成清理的资源，必须覆盖该查询并返回 true；它不是运行状态查询。
生命周期在操作收尾后、记录遥测前登记这些资源，回滚失败时连同依赖保留，允许后续
`stop()` 重试。恢复尝试先清理 pending 状态，再启动，二者共用本次尝试截止时间。
成功关闭后组件必须清除 pending 状态。MongoDB 适配器已将其 cleanup_pending 状态
接入此契约；其他适配器仍须遵守失败启动自行清理或显式报告残留的约定。

`shutdown_required() const noexcept` 单独报告停机资源归属，默认委托
`cleanup_required()`。MySQL、Redis 在维护任务登记后即报告 shutdown_required，
即使首次借用尚未成功也参与有序关闭；它们不因此报告 cleanup_required，恢复仍复用
原连接池。前者决定是否必须 stop，后者决定重试 start 前是否必须先清理，两者不能混用。

## 服务注册与依赖图

```cpp
registry.add_named<user_repository>("primary", repository);
auto& users = registry.require<user_repository>("primary");
```

- Registry 模式提供接口绑定和具名实例。
- `add_managed_named()` 以事务方式同时注册类型绑定和生命周期所有权。
- 重复注册直接失败，不静默覆盖。
- 构建后 registry 冻结并保持只读。
- 启动前使用拓扑排序检查依赖缺失与循环；同一拓扑层并行启动，停止严格逆拓扑顺序。
- `last_failure()` 保留失败组件、生命周期阶段和原始 `std::error_code`。
- 组件原始 `start()` 返回成功时立即登记资源所有权，再进行截止时间结果转换；
  即便该成功发生在取消后，调用方仍收到超时，但回滚不能漏掉该组件的 `stop()`。
- 回滚不会覆盖发起回滚的启动失败；`last_rollback_failure()` 单独提供回滚组件的关闭失败及 `rollback` 阶段。
- `last_rollback_error()` 还保留无法定位组件的回滚准备错误。回滚内部捕获清理异常并恢复原始启动错误；尚未关闭的资源保持登记，可由生命周期调用者后续重试 `stop()`。

## 自动装配清单

| 配置 `type` | 注册服务 | 说明 |
|---|---|---|
| `http_client` | `http_client_service` | 带 W3C Trace Context 的出站 HTTP 客户端 |
| `openai` | `openai_service` + `chat_model_service` | OpenAI adapter、模型连接池、`chat_model_template` 和 GenAI telemetry listener；`pool_size` 默认 4 |
| `redis` | `redis_service` / `redis_cluster_service` | `mode=standalone` 连接池；`mode=cluster` 槽路由、seed failover 与健康检查 |
| `mysql` | `mysql_service` | MySQL 连接池 |
| `postgresql` | `postgresql_service` | PostgreSQL 连接池 |
| `mongodb` | `mongodb_service` | MongoDB 连接池与维护任务 |
| `kafka` | `kafka_service` | Kafka 客户端 |
| `mqtt` | `mqtt_service` | MQTT 客户端与重连 |
| `amqp091` | `amqp091_service` | AMQP 0-9-1 客户端与受监管帧泵 |
| `amqp10` | `amqp10_service` | AMQP 1.0 客户端 |
| `grpc` | `grpc_client_service` | gRPC 客户端 |
| `grpc_server` | `grpc_server_service` | 挂载到业务 HTTP/2 路由的 gRPC 服务路由器 |

未启用相应 CMake 协议开关时，启用该服务会在构建阶段返回 `not_supported`，不会拖到运行期失败。

Standalone Redis 服务通过 `redis_service::make_template(options, parent)` 创建业务门面。
它借用服务拥有的连接池并自动使用 Application Telemetry Hub 的 span exporter；parent
仍由请求协程显式传入，框架不使用 thread-local 活动 span。返回的 `redis_template`
不能超过 `redis_service` 生命周期。Cluster 服务继续使用 `cluster_client`，当前模板不
隐式跨 slot 路由或拆分 multi-key 操作。

OpenAI 服务通过 `openai_service::make_template(options)` 或
`application_runtime::openai(instance, options)` 创建业务门面。模板借用服务拥有的模型、
共享请求门和 telemetry listener，因此不能超过 Host 生命周期；同一服务创建的多个模板
仍共享串行化边界。业务代码优先使用模板的 `invoke()` / `stream()`，不直接操作
`openai::client`，只有协议扩展端点尚未进入模板时才使用底层客户端。

AMQP 0-9-1 帧泵只监管单次连接会话，任务自身的重试预算为零，不单独触发 required
恢复耗尽通知；错误保留在任务状态中。连接重建由服务生命周期的健康恢复任务发起，
required/optional 要求及恢复预算仍来自 managed_service，而不是帧泵任务标志。
重连成功后仍需健康缓存连续成功确认，不能直接恢复 readiness。已有 open 连接只有
同时存在活动帧泵登记时才算幂等启动成功；否则必须补登记，失败进入回滚。
启动通过受监管的 `async_run_session()` 等待读取任务接管和已记录拓扑重放完成，
健康探测同时检查会话 ready、连接和帧泵；重放尚未完成时不能仅凭 socket=open 报告 up。
启动等待将阶段取消传递给会话，但成功返回后不再借用启动上下文；帧泵仍由 supervisor 拥有。
required/optional 生命周期脚本 TCP 回归扣住 Exchange.DeclareOk，验证恢复任务仍运行、
probe 为 down，释放确认后还需两次健康成功才恢复 readiness。
required/optional 的重放等待停机用例还验证：不发送交换机确认时，生命周期 stop 和
supervisor join 完成，帧泵及恢复任务均 stopped，连接 disconnected，服务登记清空，
且不触发恢复预算耗尽通知。此测试在单事件循环上发起停机，不覆盖跨线程取消竞争。
另有 200ms 恢复总预算的静默重放回归：等待交换机确认耗尽预算后，恢复错误保留
timed_out，帧泵失败、连接关闭、probe 为 down；required 升级一次，optional 不升级。
总预算与单次启动截止时间取较早者。独立阶段超时另用 200ms 启动上限、1 秒恢复预算验证：
首轮重放无确认，下一连接可观察到上轮 timed_out，再次重放参数与原声明逐字节一致；
required/optional 均恢复成功且不升级，readiness 仍需两次健康确认。范围限脚本交换机声明，
不代表队列、消费者和真实 broker 的超时恢复都已验收。
随后生命周期脚本扩展到交换机→服务端命名队列→绑定→消费者：每次连接返回不同
队列名，恢复请求检查新名称，原消费者回调接收一次带重投标志的消息并校验标签、
投递编号及正文。普通恢复及单次超时后再次恢复均走该链路；手动 ACK、完整字段表、
全部声明标志和真实 broker 仍未覆盖，不能称为完整消息可靠性验收。
AMQP 传输报告内存不足时，适配器保留通用 not_enough_memory，启动等待与帧泵状态
使用同一失败原因，不再统一返回 connection_aborted。握手、会话结果转换仅在失败
分支执行；这不代表其他协议错误类别均已完成映射审计。
这不是完整订阅链及真实 RabbitMQ 的 Application 恢复验收；启动派发失败和取消竞争仍需补齐。

## 健康与管理端点

MySQL、Redis、PostgreSQL 和 MongoDB 在启动入口拒绝已经取消或过期的
`service_context`，不先注册维护任务或进行预热。取消和过期同时发生时优先返回
`operation_canceled`。此入口检查不代替操作进行中的取消和截止时间处理。

MongoDB 预热失败保留 `cnetmod.mongodb` 错误类别，不再统一映射为连接拒绝。
类别值为协议 `error_code` 的整数值加一（协议枚举从零开始且没有成功项），
超时和取消分别转换为通用 `timed_out` 与 `operation_canceled`。
错误消息只含固定前缀及编号，不转发服务端诊断、连接凭据或命令内容。
此转换位于 Application 启动失败路径，不改变数据库查询或关闭 OTEL 的路径。

MongoDB 管理探测通过 `connection_pool::health_check(cancel_token&)` 执行实际 PING，
并使用服务截止时间取消等待。失败连接归还前标记废弃；取消通知在连接所属事件循环
执行，探测返回前等待通知收尾。无空闲连接时，在原有容量与并发建连上限内创建
候选连接，hello 和 PING 成功后才报告健康；下次可复用该连接，零初始连接池不会
仅因没有候选而一直失败。全忙时复用借用队列，受池排队超时与服务探测截止时间约束；
取消移除本次等待，不取消业务方持有的连接。业务归还后才发送 PING。
取消通知投递到所属事件循环并在探测返回前收尾；等待后需要新建连接时也传递停止信号。
池锁等待仍未支持取消，任意排队竞争、持续分配失败及真实认证恢复尚未完整验证。
池关闭将空闲连接从登记表移除，由关闭结果持有到锁外关闭；借出连接继续保留登记，
直到归还，不能通过提前清空整个登记表隐藏仍在途的借用者。

MongoDB 同步关闭复用连接槽、等待者的关闭链表，不创建临时容器或错误诊断对象。
等待者先标记为关闭，恢复后才构造原有错误。锁竞争时使用池持有的通知节点与共享
状态投递，不创建 detached 协程；`async_close()` 会等待这次已登记通知。
异步等待任务本身仍可能分配失败，调用方应保留池并重试等待；不要把同步入口无分配
理解为任意低内存状态下的全部异步清理都不会失败。事件循环必须运行到通知收尾。

MongoDB 池关闭会向所有已登记的在途建连发送取消，不再依赖 hello 命令超时。
登记节点由建连协程帧持有，完成后在池锁内移除并归还配额。`async_close()` 发出
取消但本身不等待所有建连结束；`mongodb_service::stop()` 仍检查 `connecting_count()`，
未收尾时保留清理状态，允许随后重试。启动使用同一服务截止时间与取消令牌执行
`warm_up(cancel_token&)`，取消仅转发到该次预热创建的连接；等待其他调用方建连时，
退出本次等待而不取消其他建连。Application 启动失败后的池关闭仍按资源所有权取消
全池在途建连。无令牌重载直接进入无取消特化，不增加包装协程。
本地无 hello 回复测试覆盖命令超时关闭、启动截止时间与跨线程取消，并检查可再次启动。
池锁等待仍未支持取消，真实 TLS/认证取消也尚未完成验证，不能据此宣称所有启动路径已闭合。

Kafka 服务启动把生命周期取消令牌传给客户端连接，并使用 `operation_deadline` 约束操作。
已启动服务通过同一取消/截止时间契约发起元数据刷新作为健康探测，不再仅凭启动标志报告 up。
提前取消、已过期启动，以及本地 TCP 对端收到 Kafka 请求后不响应时的超时/主动取消已覆盖回归。
真实 broker 的完整协议交换、断线恢复及其他部分连接清理仍需验证。
Kafka 取消和超时按生命周期语义返回通用错误；其他 Kafka 失败保留原始编号及
`cnetmod.kafka` 类别。配置、传输、协议格式及授权错误可与对应的 `std::errc` 条件比较。
错误消息仅包含固定前缀和编号，不转发 broker 的诊断文本。

- `/actuator/live`：进程及事件循环是否存活。
- `/actuator/ready`：应用已启动且所有已启用组件可用；optional 故障也会呈现降级 readiness。
- `/actuator/health`：聚合状态、各组件状态、错误类别和最近探测时间。
- `/actuator/prometheus`：OpenMetrics 文本指标。

默认管理地址为 `127.0.0.1:8081`。健康探测按周期并行执行并缓存结果，默认 3 次失败进入 down、2 次成功恢复 up。

运行期重连成功只进入等待健康确认状态，不计作一次成功探测；readiness 恢复仍需满足配置的连续成功探测次数。

每次受监管恢复尝试在 `start()` 成功后先执行一次 `probe()`；探测非 up 仍作为本次恢复失败处理，继续消耗该次恢复任务的预算。这次恢复内部探测不替代健康注册表的连续成功确认。

一次恢复的启动与内部探测共享 `service_start_timeout`，使用独立尝试令牌。超时只取消本次尝试，允许后续重试；应用停机取消则向当前尝试转发。上述取消仍要求服务协程协作退出。

服务恢复在任务派发前建立绝对恢复截止时间，首次连接、内部探测、后续尝试和退避均消耗同一预算；单次尝试使用该截止时间与 `service_start_timeout` 中更早者。重连成功但尚未确认健康时保留截止时间，后续恢复任务继承它。`reconcile_health()` 仅在健康缓存确认 up 且恢复任务不再运行后清除此故障周期；健康循环也检查 starting/degraded 状态下的预算到期，因此确认阶段的到期通知可能延迟一个健康刷新周期。

required 服务同一故障周期耗尽预算后不重复派发或通知停机；optional 服务保持降级，后续健康循环可在上一周期已经失败后开启新的恢复周期。预算到期仍等待被取消的协程安全退出，不强制销毁协程。

健康快照携带缓存 `revision`，每次结果更新（包括探测准备失败）都会递增。`reconcile_health()` 在修改故障周期前检查版本，忽略已过期的快照，防止延迟投递的旧 up 清除新故障预算。`is_current()` 仅提供瞬时版本检查，不会在返回后锁住健康注册表。

异步健康探测保存派发时的缓存版本，结果提交在注册表锁内比较版本并更新；期间出现更新则丢弃旧结果，不推进连续成功/失败计数。未完成探测的失败补记也使用同一版本条件。停机中的注册表不提交探测结果，stopping/stopped 组件不再被后续刷新探测。

通用 `task_supervisor::supervise()` 可选接收 `recovery_deadline`。未提供时保留后台长任务首次失败后开始恢复计时的语义；提供时用于限制重试派发和退避，操作自身仍须将同一截止时间传入其取消感知 I/O。服务生命周期已完成这一传递。

## 运行期更新

`reload_configuration()` 只原位更新日志级别、OTEL 采样率、健康策略和恢复策略。监听地址、端口、中间件、线程、连接参数、凭据、OTLP 出口或队列参数变化会设置 `restart_required`，不会偷偷重建连接。

底层 `reload_safe_configuration(active, candidate)` 返回
`std::expected<configuration_reload_result, std::error_code>`。它先校验候选并在私有副本中
准备变更清单，准备失败不修改 active；提交使用已静态验证的不抛异常移动赋值。
分配失败映射为 `not_enough_memory`，非法候选返回校验错误。调用方必须检查 expected，
不能把失败当作“没有字段变化”。这保证配置函数的提交边界，不等于 Host 向所有运行组件
传播配置已具备跨组件事务性。

同一配置绑定名若更换服务类型或实例名，旧服务的恢复策略保持不变；新身份及其策略
需要重启生效。Host 在配置锁内准备服务键及候选副本，先完成恢复策略表的批量更新，
再应用已校验的健康策略、采样率和日志级别，最后移动提交配置。底层配置函数不再
修改全局日志状态。各组件仍使用独立读锁，这不是跨组件读快照的线性一致性保证。

生命周期的 `update_recovery_policies(span<pair<service_key, recovery_policy>>)` 返回
expected：先验证策略，再复制现有覆盖表并应用整批修改，成功后锁内交换。分配失败
保留整张旧表，不再逐项提交；`recovery_policy_override(key)` 可读取指定覆盖项。
Host 检查批量更新错误，失败时不提交配置副本。临时 JSON 文档使用局部 RAII 清理器：
迭代删除叶节点后再释放空容器，不申请遍历栈、不递归，避免依赖库析构非空容器时
申请内存导致 terminate。清理需要 O(节点数 × 深度) 的最坏时间，只用于配置路径。
回归覆盖 Host 重载的 256 个分配位置，以及包含嵌套数组/对象的非法根节点的 128 个位置。
这不是任意 JSON 解析内部状态、任意集成属性副本及持续内存耗尽的完整恢复证明。

`load_configuration()` 在函数入口统一转换可传播异常：分配失败返回
`not_enough_memory`，其他未分类异常返回 `io_error`。解析和字段转换不能把
`bad_alloc` 吞成 `invalid_argument`。调用前的参数构造不在此边界内；故障注入
需先准备 `optional<filesystem::path>`，再覆盖加载函数本身。

## 停机顺序

停机先撤销 readiness，然后停止接收、排空在途 HTTP、取消并等待受监管任务、逆拓扑关闭已成功启动的服务，最后 flush Trace/Metric/Log 队列。

HTTP 请求排空返回超时时，host 保留 `timed_out`（不覆盖更早的错误），并立即中止业务和
管理连接的 socket I/O；后续 handler 完成不会把此次停机改报成功。socket 中止不能取消
任意 handler 等待，也不代表 handler 已释放服务引用。host 随后在已有整体停机截止时间内
等待被跟踪的 handler 结束，再关闭后台任务和服务；服务清理重试也不会在 handler 仍在途时
调用服务 `stop()`。预算耗尽仍有 handler 时保留服务登记。此保护不等于已解决残留协程和
资源的最终安全析构，也不覆盖未经过请求跟踪中间件的自定义工作。

排空超时还调用 `shutdown_handler::cancel_requests()`，取消被跟踪请求现有的
`request_context::cancel_pending_operations()`，同时取消直接令牌和已登记的
`request.with_deadline()` 子操作。登记节点位于协程帧中，退出时以 RAII 移除。
每个子操作仍有独立令牌；工厂按值保存在包装协程中，支持临时及仅可移动工厂。
取消是请求级终态，之后启动的子操作收到已取消令牌。请求必须活到所有子操作退出。
直接调用 `cancellation_token().cancel()` 仅取消直接令牌，不向子操作广播；请求级取消
应调用 `cancel_pending_operations()`。未传递令牌的等待不会自动停止。
取消后的子操作与被跟踪 handler 在移除登记前投递回事件循环，异常退出也保留这一边界并
重新传播原始异常，避免同步恢复的完成路径在取消调用栈中重入登记锁。未取消的完成路径
不增加这次投递。请求取消先原子发布终态，重复请求级取消直接返回；取消后创建的子操作
不进入登记链表，直接得到已取消令牌。锁内再次检查终态以处理并发登记竞争。
`shutdown_handler::cancel_requests()` 也先发布停止接收和取消终态，再广播取消；重复调用
（包括取消回调重入）直接返回，不代表首次广播已完成。新请求沿原有停机入口返回 503，
不新增正常请求入口的状态读取。登记过程中补发取消在释放登记锁后执行。
回归覆盖同步恢复后再次取消整个 shutdown handler、拒绝新请求、再次取消请求并启动
嵌套子操作。任意自定义回调及跨线程生命周期仍需进一步验证；广播期间仍持有登记锁，
不应将这一回归解释为允许任意同步重入事件循环或销毁请求。

host 在现有清理截止时间内重试仍登记的服务，退避从 1ms 增至最多 20ms；已成功关闭的服务不重复关闭。最终若仍有服务登记或活动 HTTP 连接，状态为 `cleanup_failed` 而非 `stopped`，保留原始运行错误。该状态是失败诊断，不保证任意未协作资源可安全析构；这部分仍需调用者和组件的取消/关闭契约配合。

最终连接收尾同时检查 telemetry 的取消投递是否完成，在既有预算内继续驱动完成通知；
未完成的 telemetry 工作也会阻止状态变为 stopped。单纯投递失败仍不覆盖业务结果。
该检查只发生在停机路径，不增加正常业务请求的检查或协程。

`host.retry_cleanup(timeout)` 在 `run()` 返回 cleanup_failed 后，由拥有线程独占调用。
调用方先解除可解除的占用（如归还连接租约），再传入正数预算；Host 重启同一个事件循环，
只重试清理，不重新启动监听或服务。已停止时重复调用成功，已成功关闭的依赖不会重复关闭。
返回值表示本次清理是否完成，不替换原始 run 结果；最初的业务失败仍应由调用方处理。
每次显式重试使用新预算并预留其中 20% 给最终连接收尾。顶层协程尚未结束时拒绝重试，
不能借此重入正在运行的生命周期；不协作操作、逃逸引用和并发析构仍须遵守所有权契约。

`service_lifecycle::stop(deadline budget = {})` 接收绝对截止时间，每个组件使用独立取消令牌。到期请求取消后仍等待组件协程安全退出；组件必须响应取消，框架不会强行销毁悬挂协程。关闭失败的组件及其传递依赖保留所有权，无关组件继续关闭，后续调用可重试清理。已成功关闭的组件不再重复关闭。

组件原始 `stop()` 返回成功时先移除资源登记，再转换截止时间结果。迟到完成仍向
调用者返回超时，并保留错误诊断，但组件状态为 stopped，后续收尾不重复调用 stop。
只有原始关闭失败的组件才继续保留登记；不能把超时报告等同于资源一定尚未释放。

`service_lifecycle::start(std::chrono::milliseconds rollback_reserve = {})` 可为调用方的后续收尾保留回滚预算；默认零，不改变独立生命周期调用的预算。预留必须非负且不超过整体关闭超时，否则启动前返回 `invalid_argument`。Application 传入整体关闭预算的 20%；内部回滚使用较早截止时间，`rollback_deadline()` 仍返回原始整体回滚截止时间，供后续清理共享。预留不能强制终止不响应取消的服务。

当前 HTTP 排空、服务关闭及 telemetry flush 的投递预算共享正常停机截止时间；flush 不会重新获得完整配置时长。HTTP 绑定失败的清理以及服务启动失败的内部回滚，也将已建立的清理截止时间传给 host 的 flush；`rollback_deadline()` 可读取最近一次回滚预算，每次新启动前重置。投递时间归零后仍保留至少 1ms 取消收尾机会。最终连接等待及中止后的等待也受同一整体截止时间约束，不会重新获得完整 drain/stop 时长；等待保留亚毫秒精度。任务等待、预算耗尽后的残留资源析构及异常清理路径仍需进一步验证；不得将配置的超时解释为已经验证的进程级硬退出上限。

正常停机的请求排空、handler 收尾、服务关闭及其重试、telemetry 投递使用较早的清理截止时间，为最终连接收尾保留配置整体预算的 20%；各阶段不会重新计算出一段完整预算。最终连接等待又为 socket 中止后的完成回调预留进入该阶段时剩余预算的 20%，而不是等整体预算耗尽才中止连接。50ms 整体预算、200ms 阶段上限、依赖关闭等待 10 秒并响应取消的半截 HTTP 请求回归覆盖该路径。未协作的任务等待、启动回滚和异常路径仍可能耗尽预算，不能据此推断所有路径已具备硬退出上限。

请求排空每次等待取 50ms 与剩余预算中的较小值，零预算不投递等待。`drain()` 的睡眠
回调应接受 `std::chrono::steady_clock::duration`（推荐泛型 `auto duration`），避免截断
亚毫秒剩余时间。预算限制的是投递等待时长，不保证操作系统调度不会产生额外延迟。

## 设计模式与边界

- `application_builder`：Builder；负责声明配置和组合。
- `application_host`：Facade / Composition Root；独占运行时资源。
- `service_registry`：Registry；只在构建阶段可变。
- `managed_service`：Adapter；统一异构基础设施生命周期。
- `service_lifecycle`：依赖图协调器与补偿事务。
- `task_supervisor`：Supervisor；负责重启、预算和错误传播。
- `health_registry`：状态机和缓存视图。
- 自动装配 registry：显式启用的 Strategy 集合。

业务模块应继续通过构造函数依赖明确接口，不要把 `service_registry` 当作全局 Service Locator。
# JWT operations

`application_runtime::sign_jwt(options, secret)` and
`application_runtime::verify_jwt(token, secret)` use the managed CPU executor
and resume on the application event loop. The application retains ownership of
the secret and passes it only at the call site.
