# cnetmod Redis / OpenAI / Application 下游升级说明

本文用于通知依赖 cnetmod 的下游项目。升级目标是消除 Redis 大响应分片、
OpenAI SSE 挂起和脏连接复用问题，并补齐 Application 自定义服务与 MySQL
自动装配配置。下游不应继续维护相同的本地补丁。

## 修复内容

### Redis RESP3 分片读取

- `client::cmd()`、`exec()`、pipeline 及嵌套 aggregate 读取在 bulk body 未完整到达时，
  不再推进接收游标或重置 `resp3_parser`。
- parser 会保留 bulk 类型、长度及累计偏移，直到完整 value 到达后才提交游标。
- 命令交换失败、协议错误、意外尾随响应或未读完整响应时，连接会关闭；连接池只接收
  `is_reusable()` 为真的连接，不会仅凭 socket 仍 open 就回收。

这解决了响应较大并跨多个 socket read 到达时，小响应成功而约 12 KB/38 KB 响应失败的
问题，也阻止残留字节污染下一条命令。

下游动作：删除围绕 `cmd()` 的临时重试、响应大小分流和“出错后仍归还连接”逻辑。
正常业务可继续使用原有 `cmd()`/`exec()` API；需要显式取消和响应上限时使用
`exchange(request, cancel_token&, response_byte_limit)`。

### OpenAI SSE 生命周期

- `chat_stream()` 和 `chat_stream_async()` 新增接受 `cancel_token&` 的重载。
- SSE delta 在完整 event 到达后立即回调，不等待 `[DONE]` 或对端关闭连接。
- 非空 `finish_reason` 仍作为语义完成条件；请求 usage 时只额外有界等待 usage 尾帧。
- 每次流式网络读取受 `connect_options::timeout_seconds` 限制。
- 调用方取消、读取超时、写入失败、解析失败或回调提前停止都会关闭当前连接，下一次
  请求自动建立干净连接。

推荐迁移：

```cpp
cnetmod::cancel_token cancellation;
auto result = co_await client.chat_stream_async(request,
    [](const cnetmod::openai::chat_chunk& chunk) -> cnetmod::task<bool>
    {
        co_await forward_delta(chunk);
        co_return true;
    },
    cancellation);
```

旧的两参数调用仍可编译，并使用客户端配置的读取超时。下游不需要等待 `[DONE]`，也不应
把所有 delta 缓存到完整响应后再转发。

### Application 自定义服务组合

新增 `application_builder::service_factory()`。工厂在配置解析和校验完成后、服务 registry
冻结前执行，通过 `application_service_context` 获得：

- host 所有的 `io_context`；
- Telemetry Hub；
- `task_supervisor`；
- 只读的最终应用配置。

工厂返回 `std::expected<std::shared_ptr<managed_service>, std::error_code>`。错误、空服务、
重复服务身份或无效依赖都会让 `build()` 失败。框架没有增加运行期
`application_host::io()` 访问器，避免下游绕过生命周期和后台任务监管。

推荐迁移：

```cpp
auto host = cnetmod::application::application_builder{"orders"}
    .service_factory([](cnetmod::application::application_service_context& context)
        -> std::expected<std::shared_ptr<cnetmod::application::managed_service>,
            std::error_code>
    {
        return std::make_shared<orders_service>(
            context.io, context.telemetry, context.supervisor);
    })
    .build();
```

关键后台协程仍必须由返回的 `managed_service` 或 `context.supervisor` 管理。

### MySQL 自动装配

MySQL service properties 新增：

| 配置项 | 类型/范围 | 说明 |
|---|---|---|
| `ssl` | `disable` / `enable` / `require` | TLS 模式 |
| `tls_verify` | boolean | 是否校验证书 |
| `tls_ca_file` | string | 自定义 CA 文件 |
| `connect_timeout_ms` | 1～86400000 | 建连超时 |
| `pool_timeout_ms` | 1～86400000 | 获取连接超时 |
| `retry_interval_ms` | 1～86400000 | 维护重试间隔 |
| `ping_interval_ms` | 1～86400000 | 空闲探测周期 |
| `ping_timeout_ms` | 1～86400000 | 探测超时 |

未知 TLS 模式、错误 JSON 类型、零值、负数和超范围时长都会在 `build()` 阶段失败。
未配置的新字段继续使用 `mysql::pool_params` 原默认值。

### `sync_wait()` 语义

`sync_wait()` 不运行 `io_context`。只有纯协程计算，或 I/O 事件循环已经在其他线程运行时
才能使用。应用主入口应采用 `spawn(ctx, task)` 后调用 `ctx.run()`；不要用
`sync_wait()` 等待依赖尚未运行事件循环的网络、定时器或文件 I/O。

它也不负责桥接阻塞 API 或其他协程库。阻塞调用应通过 `thread_pool`、`spawn_on` 或
`blocking_invoke` 卸载，第三方 awaitable 应通过 `from_awaitable` 转换；执行域与生命周期
规则见 [Executor 与 Bridge](../skill/coro/executor-bridge.md)。

## 兼容性与下游检查清单

1. 更新 cnetmod 依赖到包含本说明的版本，并重新生成 C++ Modules 依赖图。
2. 删除下游 `third_party/cnetmod` 内的同类临时补丁，禁止同时保留两套游标修正。
3. 如使用 OpenAI 流式接口，为请求生命周期传入 `cancel_token&`；保留两参数调用也兼容。
4. 将手工获取 host event loop 的自定义组件迁移到 `service_factory()`。
5. 如 MySQL 配置包含 TLS/超时字段，使用上述精确名称和整数毫秒值。
6. 检查入口是否错误地用 `sync_wait()` 驱动 I/O 或桥接第三方库；分别改为
   `spawn + io_context::run()` 或采用 Executor 与 Bridge 文档规定的桥接 API。
7. 运行下游自己的 Redis 大 bulk、OpenAI 长连接 SSE、取消、超时及停机回归。

## 上游验证记录

- Arch Linux / Clang 22.1.8 / libc++ / io_uring / BoringSSL QUIC：完整构建成功。
- CI 同等的非外部测试集合：89/89 通过，0 skip，耗时 71.68 秒。
- 所有协议关闭、ORM 关闭的纯核心模块图：`cnetmod_core` 660 个构建步骤成功。
- 定向测试：`test_application`、`test_application_integrations`、`test_openai`、
  `test_redis` 全部通过。
- 新增回归覆盖：13,000 字节 scalar bulk、aggregate child 跨多次读取；SSE 挂起读取取消
  与连接淘汰；host-owned service factory；MySQL TLS/超时配置合法与非法输入。
- `AGENTS.md` 已由全部 skill 源重新生成并通过 `tools/generate_agents.py --check`。

外部 MySQL、Redis Cluster 和消息 broker 的 live 测试不包含在上述 89 项非外部集合内；
它们应由具备对应服务的集成环境继续执行。未定义的缺陷编号 F7 不在本次变更范围内，
如需处理必须先提供可复现描述、期望行为和影响 API。
