# JWT 签发与验证

> 协程原生 JWT 模块，基于 jwt-cpp，CPU 密集操作卸载到 stdexec 线程池。
> 模块: `import cnetmod.security.jwt;`

## 核心原则

- `sign_jwt()` / `verify_jwt()` 均为 `task<T>` 协程接口
- CPU 密集的签名/验证操作自动卸载到 `thread_pool`，不阻塞 IO 线程
- 使用 `std::expected<T, std::string>` 返回结果
- 当前仅支持 HS256（HMAC-SHA256 对称签名）

## 1. jwt_algorithm — 签名算法

```cpp
enum class jwt_algorithm
{
    hs256,  // HMAC-SHA256（对称）
    // rs256 预留，未来支持 RSA-SHA256
};
```

## 2. jwt_claims — JWT 声明

```cpp
struct jwt_claims
{
    std::string subject;
    std::string issuer;
    std::vector<std::string> scopes;
    std::chrono::system_clock::time_point issued_at;
    std::chrono::system_clock::time_point expires_at;
    /// 所有非标准自定义声明
    std::map<std::string, std::string> custom;
};
```

| 字段 | 标准 JWT Claim | 说明 |
|------|----------------|------|
| `subject` | `sub` | 主题（通常是用户 ID） |
| `issuer` | `iss` | 签发者 |
| `scopes` | 自定义 | 权限范围列表 |
| `issued_at` | `iat` | 签发时间 |
| `expires_at` | `exp` | 过期时间 |
| `custom` | 自定义 | 额外键值对 |

## 3. jwt_sign_options — 签发参数

```cpp
struct jwt_sign_options
{
    std::string issuer;
    std::string subject;
    std::vector<std::string> scopes;
    std::chrono::system_clock::duration lifetime = std::chrono::hours(1);
    jwt_algorithm algorithm = jwt_algorithm::hs256;
    /// 注入到 payload 的额外自定义声明
    std::map<std::string, std::string> custom_claims;
};
```

## 4. sign_jwt — 签发 JWT

```cpp
auto sign_jwt(thread_pool& pool, io_context& io,
              const jwt_sign_options& opts, std::string_view secret)
    -> task<std::expected<std::string, std::string>>;
```

| 参数 | 说明 |
|------|------|
| `pool` | stdexec 线程池，用于卸载 CPU 密集操作 |
| `io` | io_context，完成后返回 IO 线程 |
| `opts` | 签发参数（issuer、subject、lifetime 等） |
| `secret` | HS256 密钥（或未来 RS256 的 PEM 私钥） |
| **返回** | JWT 字符串（`header.payload.signature`），失败返回错误信息 |

### 示例

```cpp
import std;
import cnetmod.security.jwt;
import cnetmod.executor.pool;
import cnetmod.io;

auto token_result = co_await cnetmod::security::sign_jwt(pool, io, {
    .issuer = "myapp",
    .subject = "user123",
    .scopes = {"read", "write"},
    .lifetime = std::chrono::hours(24)
}, "super-secret-key");

if (token_result)
    std::println("JWT: {}", *token_result);
else
    std::println("签发失败: {}", token_result.error());
```

## 5. verify_jwt — 验证 JWT

```cpp
auto verify_jwt(thread_pool& pool, io_context& io,
                std::string_view token, std::string_view secret)
    -> task<std::expected<jwt_claims, std::string>>;
```

| 参数 | 说明 |
|------|------|
| `pool` | stdexec 线程池 |
| `io` | io_context |
| `token` | 编码的 JWT 字符串（`header.payload.signature`） |
| `secret` | HS256 验证密钥 |
| **返回** | 解析后的 `jwt_claims`，失败返回错误信息 |

### 示例

```cpp
auto claims_result = co_await cnetmod::security::verify_jwt(
    pool, io, token_value, "super-secret-key");

if (claims_result)
{
    auto& claims = *claims_result;
    std::println("用户: {}, 权限: {}", claims.subject, claims.scopes.size());

    if (!cnetmod::security::is_jwt_expired(claims))
        std::println("Token 有效");
    else
        std::println("Token 已过期");
}
else
{
    std::println("验证失败: {}", claims_result.error());
}
```

## 6. is_jwt_expired — 过期检查

```cpp
[[nodiscard]] inline auto is_jwt_expired(const jwt_claims& claims) -> bool
{
    return std::chrono::system_clock::now() > claims.expires_at;
}
```

轻量级检查，无密码学开销。

## 7. 线程池卸载模式

JWT 签名/验证涉及 HMAC-SHA256 计算，属于 CPU 密集操作。cnetmod 通过 `thread_pool` + `blocking_invoke` 将其从 IO 线程卸载:

```cpp
// 1. 创建线程池和 IO 上下文
auto ctx = cnetmod::make_io_context();
cnetmod::thread_pool pool;

// 2. 在协程中调用（自动卸载到线程池）
auto work = [&]() -> cnetmod::task<void>
{
    // sign_jwt 内部自动: IO线程 → 线程池执行加密 → 回到IO线程
    auto token = co_await cnetmod::security::sign_jwt(pool, *ctx, {
        .issuer = "myapp",
        .subject = "user123",
        .lifetime = std::chrono::hours(1)
    }, "secret");

    // verify_jwt 同理
    if (token)
    {
        auto claims = co_await cnetmod::security::verify_jwt(
            pool, *ctx, *token, "secret");
        // ...
    }
};

cnetmod::spawn(*ctx, work());
ctx->run();
```

## 8. 完整示例：HTTP 中间件中的 JWT

```cpp
import std;
import cnetmod.security.jwt;
import cnetmod.protocol.http;
import cnetmod.executor.pool;

auto jwt_middleware(cnetmod::thread_pool& pool, std::string_view secret)
{
    return [&](cnetmod::http::request& req, cnetmod::http::response& res,
               auto next) -> cnetmod::task<void>
    {
        auto auth = req.header("Authorization");
        if (!auth || !auth->starts_with("Bearer "))
        {
            res.status(401).body("Missing token");
            co_return;
        }

        auto token = auth->substr(7);
        auto result = co_await cnetmod::security::verify_jwt(
            pool, req.io_context(), token, secret);

        if (!result)
        {
            res.status(401).body("Invalid token");
            co_return;
        }

        if (cnetmod::security::is_jwt_expired(*result))
        {
            res.status(401).body("Token expired");
            co_return;
        }

        // 将用户信息注入请求上下文
        req.set("user_id", result->subject);
        co_await next();
    };
}
```

## CMake 依赖

JWT 模块位于 `cnetmod_core` 静态库中，无需额外 CMake 开关。
依赖 `3rdparty/jwt-cpp`（已内置）。
