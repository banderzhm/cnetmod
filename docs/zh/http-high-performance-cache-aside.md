# C++23 Cache-Aside：Application Repository + RedisTemplate

当前 cnetmod 的业务持久化入口是 `application_runtime::repository<T>()`，缓存入口是 Application 托管的 `redis_service::make_template()`。业务代码不直接持有数据库连接、原始 ORM Session 或 Redis 协议连接。

## 依赖关系

```text
HTTP Handler
  -> user_service
    -> application_repository<user_profile>
    -> redis_template
```

`application_repository<T>` 管理数据库连接租约、事务、自动策略和遥测；`redis_template` 管理键命名空间、TTL、nil/错误区分以及连接可复用性。

## 组合

```cpp
auto users = host->runtime().repository<user_profile>("primary");
if (!users)
    return EXIT_FAILURE;

auto cache_service = host->services().require<
    cnetmod::application::redis_service>("cache");
if (!cache_service)
    return EXIT_FAILURE;

auto cache = cache_service->get().make_template({
    .ns = {.prefix = "user:profile:"},
    .default_ttl = std::chrono::minutes{5},
});
```

## 读取流程

1. 用 `redis_template::get()` 查询缓存。
2. `optional{nullopt}` 表示缓存未命中；错误表示 Redis 故障，两者不能混淆。
3. 缓存未命中或 Redis 可降级错误时，调用 `repository<T>::get_by_id()`。
4. 数据库错误返回服务不可用，成功空结果返回未找到。
5. 查询成功后使用 `redis_template::set()` 回填；回填失败不改变本次数据库读取结果。

示意代码：

```cpp
auto load_user(cnetmod::application::managed_repository<user_profile>& users,
    cnetmod::redis::redis_template& cache, std::int64_t id)
    -> cnetmod::task<std::expected<std::optional<user_profile>, std::error_code>>
{
    const auto suffix = std::to_string(id);
    auto cached = co_await cache.get(suffix);
    if (cached && cached->has_value())
        co_return decode_user(**cached);

    auto selected = co_await users.get_by_id(
        cnetmod::orm::param_value::from_int(id));
    if (selected.is_err())
        co_return std::unexpected(selected.framework_error);

    auto value = selected.first();
    if (!value)
        co_return std::optional<user_profile>{};

    auto encoded = encode_user(*value);
    (void)co_await cache.set(suffix, encoded);
    co_return value;
}
```

JSON 编解码在实际应用中通过 `application_runtime::json()` 卸载到 Application CPU 池，不在 I/O 协程中同步执行重 CPU 工作。

## 写入一致性

写请求先通过 Repository 完成数据库事务，提交成功后再删除或更新缓存键。不要在数据库提交前发布缓存值；需要严格一致性时使用 outbox/CDC，而不是把 Redis 操作伪装成跨资源事务。

## 并发与容量

- 热点 miss 使用 single-flight 合并，避免缓存击穿。
- TTL 增加抖动，避免批量同时过期。
- 数据库和 Redis 连接池按后端容量配置，而不是按 HTTP 并发数无限扩张。
- Handler 只编排异步调用，不阻塞事件循环。
- 默认遥测不记录缓存值、SQL 参数或用户正文。

ORM 的完整当前接口见 [`advanced/orm-guide.md`](advanced/orm-guide.md)，RedisTemplate 接口见 `skill/database/redis.md`。
