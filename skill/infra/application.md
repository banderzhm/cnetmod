# Application 应用运行框架

> 提供配置、显式自动装配、依赖生命周期、任务监管、健康检查、OTEL 和优雅停机的生产级组合根。

**import**: `import cnetmod.application;`

**源码**: `src/application/`

## 核心原则

1. 使用 `application_builder` 构建，使用 `application_host` 运行；不存在旧 `http_application` 兼容层。
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

```cpp
#include <cnetmod/config.hpp>

import std;
import cnetmod.application;

auto configure_routes(cnetmod::http::router& routes) -> void
{
    routes.get("/orders", [](cnetmod::http::request_context& request)
        -> cnetmod::task<void>
    {
        request.json(cnetmod::http::status::ok, R"({"orders":[]})");
        co_return;
    });
}

auto main() -> int
{
    auto host = cnetmod::application::application_builder{"order-service"}
        .configuration_file("application.json")
        .enable_auto_configuration()
        .routes(configure_routes)
        .build();
    if (!host)
        return EXIT_FAILURE;
    return host->run() ? EXIT_SUCCESS : EXIT_FAILURE;
}
```

`application_host` 自己创建 `net_init`、`io_context`、HTTP 服务、Telemetry Hub、健康缓存和任务监管器。`request_stop()` 可由其他线程重复调用，所有调用汇入同一条幂等停机路径。

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

优先级固定为：框架默认值 < JSON/YAML < 环境变量 < builder `configure()` 显式覆盖。

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

```json
{
  "application": {
    "name": "order-service",
    "install_signal_handlers": true
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
    "request_timeout_ms": 30000
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

服务条目的对象键只是配置绑定名；`type + instance` 才是服务身份，因此同一接口可配置多个具名实例。未知框架字段、未知集成属性、非法端口、非法超时、重复服务身份和缺失的必要凭据都会使 `build()` 失败。

HTTP client 的 `connect_timeout_ms`、`request_timeout_ms` 和 OpenAI 的
`timeout_seconds` 在创建对应服务前检查整数类型及正数范围，不接受布尔、小数、
字符串、null、零、负数或超范围整数，避免 JSON 转换产生截断或回绕。
缺省字段仍使用原有默认值。此校验不表示其他所有集成参数均已完成边界审计。

Redis、MySQL、PostgreSQL、MongoDB、Kafka、MQTT、AMQP 0-9-1 和 AMQP 1.0
的独立 `port` 属性同样在转换前检查，必须为 1～65535 的整数；缺省保持协议默认端口。
不能依赖无符号转换后的端口值做合法性判断，否则 65537 等值可能回绕为另一个端口。

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
| `openai` | `openai_service` | OpenAI 客户端和 GenAI telemetry listener |
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
