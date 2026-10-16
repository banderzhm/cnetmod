# 口令哈希

> 使用 BoringSSL 的 PBKDF2-HMAC-SHA256 提供随机盐、常量时间校验和哈希策略升级检测。
> 模块：`import cnetmod.security.password;`，也可使用聚合模块 `import cnetmod.security;`。
> 源码位于 `src/utils/security/`；目录归属不改变公开模块名。

## 核心原则

- 业务不得自行实现或复制口令派生、编码解析和常量时间比较。
- 请求协程中使用线程池重载，避免 PBKDF2 阻塞事件循环。
- 编码包含算法、迭代次数、盐和派生摘要，可直接持久化到密码字段。
- 每次哈希都从 CSPRNG 生成新盐；同一口令的两次结果应不同。
- Argon2 不由 BoringSSL 提供。框架不自行实现密码算法；引入经过审计的提供者后才能新增该枚举值。

## API

```cpp
enum class password_hash_algorithm
{
    pbkdf2_sha256,
};

struct password_hash_options
{
    password_hash_algorithm algorithm = password_hash_algorithm::pbkdf2_sha256;
    std::uint32_t iterations = 210000;
    std::size_t salt_bytes = 16;
    std::size_t digest_bytes = 32;
};

auto hash_password(std::string_view password,
    password_hash_options options = {})
    -> std::expected<std::string, std::error_code>;
auto verify_password(std::string_view password,
    std::string_view encoded) noexcept -> bool;
auto password_hash_needs_rehash(std::string_view encoded,
    password_hash_options options = {}) noexcept -> bool;

auto hash_password(thread_pool& pool, io_context& request_loop,
    std::string password, password_hash_options options = {})
    -> task<std::expected<std::string, std::error_code>>;
auto verify_password(thread_pool& pool, io_context& request_loop,
    std::string password, std::string encoded) -> task<bool>;
```

同步重载用于 CPU 工作线程、离线工具或测试。HTTP handler 必须使用异步重载，并把当前请求所在的事件循环作为 `request_loop`；多事件循环应用中不要传控制循环。

## 示例

```cpp
import std;
import cnetmod.security.password;

auto create_password(cnetmod::thread_pool& cpu,
    cnetmod::io_context& request_loop, std::string password)
    -> cnetmod::task<std::expected<std::string, std::error_code>>
{
    co_return co_await cnetmod::security::hash_password(
        cpu, request_loop, std::move(password));
}

auto authenticate(cnetmod::thread_pool& cpu,
    cnetmod::io_context& request_loop, std::string password,
    std::string stored_hash) -> cnetmod::task<bool>
{
    co_return co_await cnetmod::security::verify_password(
        cpu, request_loop, std::move(password), std::move(stored_hash));
}
```

登录成功后可调用 `password_hash_needs_rehash()`。返回 `true` 时，用当前策略重新哈希并更新数据库，实现无停机迭代次数升级。

## CMake

口令哈希依赖框架的 BoringSSL 密码学提供者，需要 `CNETMOD_ENABLE_SSL=ON`。关闭 SSL 时不会导出 `cnetmod.security`、`cnetmod.security.jwt` 或 `cnetmod.security.password`。
