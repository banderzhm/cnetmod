# 定时器、重试与断路器

> 提供异步定时器、自动重试和断路器模式，增强网络应用的可靠性。

**import**: `import cnetmod.coro;`
**源码**: `src/coro/timer.cppm`, `src/coro/retry.cppm`, `src/coro/circuit_breaker.cppm`

## 场景导航

| 场景 | 推荐 API |
|------|----------|
| 等待一段时间后执行 | `async_sleep()` / `steady_timer` |
| 等待到指定时间点 | `async_sleep_until()` / `high_resolution_timer` |
| 给异步操作加超时限制 | `with_timeout()` |
| 失败后自动重试（指数退避） | `retry()` |
| 失败后重试（抛异常风格） | `retry_throwing()` |
| 防止级联故障（熔断） | `circuit_breaker` |

---

## API 参考

### 定时器类

#### `steady_timer` — 标准精度定时器

基于 `io_context` 的异步定时器，使用平台原生定时器。

```cpp
export class steady_timer {
public:
    explicit steady_timer(io_context& ctx) noexcept;
    auto async_wait(std::chrono::steady_clock::duration duration)
        -> task<std::expected<void, std::error_code>>;
    [[nodiscard]] auto context() noexcept -> io_context&;
};
```

#### `high_resolution_timer` — 高精度定时器

支持等待到指定时间点。

```cpp
export class high_resolution_timer {
public:
    explicit high_resolution_timer(io_context& ctx) noexcept;
    auto async_wait_until(std::chrono::steady_clock::time_point deadline)
        -> task<std::expected<void, std::error_code>>;
    auto async_wait(std::chrono::steady_clock::duration duration)
        -> task<std::expected<void, std::error_code>>;
    [[nodiscard]] auto context() noexcept -> io_context&;
};
```

---

### 便捷函数

#### `async_sleep` — 异步休眠

抛异常的便捷封装。如需显式错误处理，请使用定时器类。

```cpp
export auto async_sleep(io_context& ctx,
    std::chrono::steady_clock::duration duration) -> task<void>;

export auto async_sleep_until(io_context& ctx,
    std::chrono::steady_clock::time_point tp) -> task<void>;
```

---

### `with_timeout` — 超时包装

为异步操作添加超时控制：

```cpp
export template <typename T>
auto with_timeout(io_context& ctx, std::chrono::steady_clock::duration timeout,
    task<std::expected<T, std::error_code>> op, cancel_token& op_token)
    -> task<std::expected<T, std::error_code>>;
```

超时后通过 `cancel_token` 取消被包装的操作，通常返回 `errc::operation_aborted`。内部用 `when_all` 并行启动定时器和操作任务，任一完成即取消另一方。

---

### 重试机制

#### `retry_options` — 重试配置

```cpp
export struct retry_options {
    std::uint32_t max_attempts = 3;           ///< 最大尝试次数（含首次）
    std::chrono::steady_clock::duration
        initial_delay = std::chrono::milliseconds(100); ///< 首次失败后延迟
    std::chrono::steady_clock::duration
        max_delay = std::chrono::seconds(5);  ///< 延迟上限
    double multiplier = 2.0;                  ///< 退避乘数
    bool jitter = true;                       ///< 添加随机 ±25% 抖动
};
```

延迟计算方式：每次失败后延迟乘以 `multiplier`，但不超过 `max_delay`。
启用 `jitter` 时，实际延迟在当前值的 75%–125% 之间随机浮动，避免惊群效应。

#### `retry` — 重试（expected 风格）

```cpp
// 返回 task<expected<T, E>> 的操作
export template <typename T, typename E, typename Fn>
requires std::invocable<Fn> &&
             std::same_as<std::invoke_result_t<Fn>, task<std::expected<T, E>>>
auto retry(io_context& ctx, retry_options opts, Fn fn)
    -> task<std::expected<T, E>>;

// task<expected<void, E>> 特化重载
export template <typename Fn>
requires std::invocable<Fn> &&
             detail::is_task_expected_void<std::invoke_result_t<Fn>>
auto retry(io_context& ctx, retry_options opts, Fn fn)
    -> std::invoke_result_t<Fn>;
```

对返回 `task<std::expected<T, E>>` 的异步操作进行重试。
遇到成功立即返回；达到 `max_attempts` 后返回最后一次错误。

#### `retry_throwing` — 重试（异常风格）

对通过异常报告失败的异步操作进行重试。成功时返回结果；所有尝试失败后重新抛出最后一次异常。

---

### 断路器

#### `circuit_breaker_options` — 断路器配置

```cpp
export struct circuit_breaker_options {
    std::uint32_t failure_threshold = 5;  ///< 触发熔断的失败次数
    std::uint32_t success_threshold = 2;  ///< half_open 状态恢复所需的成功次数
    std::chrono::steady_clock::duration
        timeout = std::chrono::seconds(30); ///< open 状态等待时间
};
```

#### `circuit_breaker_state` — 断路器状态枚举

```cpp
export enum class circuit_breaker_state : std::uint8_t {
    closed,    ///< 正常 — 请求通过，追踪失败
    open,      ///< 熔断 — 请求立即拒绝
    half_open, ///< 探测 — 允许有限请求以测试恢复
};
```

#### `circuit_breaker_errc` — 断路器错误码

```cpp
export enum class circuit_breaker_errc {
    success = 0,
    circuit_open, ///< 断路器处于 open 状态，请求被拒绝
};
```

#### `circuit_breaker` — 三态断路器

```cpp
export class circuit_breaker {
public:
    explicit circuit_breaker(circuit_breaker_options opts = {}) noexcept;
    template <typename T, typename E, typename Fn>
    auto execute(Fn fn) -> task<std::expected<T, E>>;
    template <typename T, typename Fn>
    auto execute_ec(Fn fn) -> task<std::expected<T, std::error_code>>;
    [[nodiscard]] auto state() const noexcept -> circuit_breaker_state;
    void reset() noexcept;
    void trip() noexcept;
};
```

**状态转换**：closed → open（失败累积）→ half_open（超时自动恢复）→ closed（成功验证）。
open 状态下立即返回错误；从 open 到 half_open 在下次 execute 时自动检查。

---

## Do's & Don'ts

### Do's

- **定时器用 `async_sleep` 快速开始**：简单延时最简洁
- **需要错误码时用定时器类**：`steady_timer` / `high_resolution_timer` 返回 `std::expected`
- **给网络 IO 加超时**：使用 `with_timeout()` 包装长时间运行的读写操作
- **重试配合断路器**：对远程服务调用同时使用 `retry()` + `circuit_breaker`
- **合理设置 `jitter`**：多实例场景下保持 `jitter = true`，防止重试风暴

### Don'ts

- **不要在热循环中使用 `async_sleep`**：高频调用请用计数器或令牌桶
- **不要把 `retry` 用于幂等性不满足的操作**：确保操作可安全重复
- **不要忽略 `with_timeout` 的 `cancel_token`**：超时后操作不会自动停止
- **不要在 open 状态下强行调用**：返回 `circuit_open` 时应在上层做降级处理
- **不要共用 `circuit_breaker` 实例跨不同服务**：每个下游服务应有独立的断路器

---

## 参考示例

### 定时器用法

```cpp
import std;
import cnetmod.coro;
import cnetmod.io;

using namespace cnetmod;

// 简易延时
co_await async_sleep(ctx, 300ms);

// 精确控制定时器
steady_timer timer(ctx);
auto result = co_await timer.async_wait(500ms);
if (!result) {
    std::println("timer error: {}", result.error().message());
}

// 等待到特定时间点
high_resolution_timer hr(ctx);
auto deadline = std::chrono::steady_clock::now() + 150ms;
co_await hr.async_wait_until(deadline);
```

### 超时控制

```cpp
cancel_token token;
auto result = co_await with_timeout(ctx, 5s,
    async_read_some(ctx, socket, buffer, token), token);

if (!result) {
    std::println("timeout: {}", result.error().message());
}
```

### 重试机制

```cpp
// Expected 风格
auto result = co_await retry(ctx, {
    .max_attempts = 5,
    .initial_delay = 200ms,
    .max_delay = 10s,
    .multiplier = 3.0,
    .jitter = true,
}, [&]() {
    return http_get("https://api.example.com/data");
});

// 异常风格
try {
    auto data = co_await retry_throwing(ctx, {
        .max_attempts = 3,
        .initial_delay = 100ms
    }, [] {
        return parse_config();
    });
} catch (const std::exception& e) {
    std::println("retry failed: {}", e.what());
}
```

### 断路器模式

```cpp
circuit_breaker cb({
    .failure_threshold = 5,
    .success_threshold = 2,
    .timeout = 30s,
});

auto result = co_await cb.execute_ec([]() {
    return db_query("SELECT * FROM users");
});

if (result.error() == make_error_code(circuit_breaker_errc::circuit_open)) {
    // 执行降级逻辑
}
```

> **完整示例**: `examples/core/timer_demo.cpp`
