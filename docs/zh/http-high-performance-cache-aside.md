# C++23 高性能用户接口实战：cnetmod + Redis + MySQL

这是一份可直接用于 CSDN 的示例：多核 HTTP 服务、按 worker 分片的 Redis/MySQL 连接池，以及 cache-aside（缓存旁路）读路径。

它没有使用 `acquire_session()`、`redis_conn->get()` 或 `setex()`——这些不是当前 cnetmod 的公开 API。代码使用已实现的 `sharded_connection_pool::async_get_connection()`、`redis::client::cmd()`，并通过 `orm::mysql_session::find_by_id` 将数据库行自动映射到实体。

## 为什么它快

- 请求等待 Redis/MySQL 时会挂起协程，不阻塞 I/O worker。
- Redis 与 MySQL 均按 worker 分片，优先在当前 `io_context` 上取连接，减少跨线程共享。
- 缓存命中只做一次 RESP3 GET 与 JSON 反序列化；未命中才查询 MySQL，并以 `SET EX` 回填。
- 路由 ID 经 `std::from_chars` 校验后才进入 ORM 主键查询，错误的 ID 不会触及数据库。
- Redis 故障自动降级到 MySQL；MySQL 故障明确返回 503，避免误报 404。

## 完整代码

```cpp
#include <cnetmod/config.hpp>
#include <cnetmod/orm.hpp>

import std;
import nlohmann.json;
import cnetmod.core;
import cnetmod.coro;
import cnetmod.io;
import cnetmod.executor;
import cnetmod.protocol.http;
import cnetmod.protocol.mysql;
import cnetmod.protocol.redis;
import cnetmod.orm;

namespace cn = cnetmod;
namespace http = cn::http;
namespace mysql = cn::mysql;
namespace redis = cn::redis;
namespace orm = cn::orm;
using json = nlohmann::json;

struct user_profile {
    std::int64_t id{};
    std::string username;
    std::string email;
    std::int32_t role_id{};

};

CNETMOD_MODEL(user_profile, "users",
    CNETMOD_FIELD(id, "id", bigint, PK),
    CNETMOD_FIELD(username, "username", varchar),
    CNETMOD_FIELD(email, "email", varchar),
    CNETMOD_FIELD(role_id, "role_id", int_))

class user_service {
public:
    user_service(mysql::sharded_connection_pool& mysql_pool,
                 redis::sharded_connection_pool& redis_pool)
        : mysql_pool_(mysql_pool), redis_pool_(redis_pool) {}

    auto get_by_id(cn::io_context& io, std::int64_t id)
        -> cn::task<std::expected<std::optional<user_profile>, std::error_code>> {
        const auto key = std::format("user:profile:{}", id);

        // 1) Redis：故障或脏缓存均安全降级到 MySQL。
        if (auto conn = co_await redis_pool_.async_get_connection(io); conn) {
            auto cached = co_await (*conn)->cmd({"GET", key});
            if (cached && !cached->empty() && !cached->front().is_null() &&
                !cached->front().is_error()) {
                const auto parsed = json::parse(redis::first_value(*cached), nullptr, false);
                if (!parsed.is_discarded()) {
                    if (auto user = orm::from_json<user_profile>(parsed); user)
                        co_return std::optional<user_profile>{std::move(*user)};
                }
            }
        }

        // 2) ORM 按主键查询并自动完成 MySQL 行 -> user_profile 映射。
        auto conn = co_await mysql_pool_.async_get_connection(io);
        if (!conn)
            co_return std::unexpected(std::make_error_code(std::errc::resource_unavailable_try_again));
        orm::mysql_session db(conn->get());
        auto rows = co_await db.find_by_id<user_profile>(orm::param_value::from_int(id));
        if (rows.is_err())
            co_return std::unexpected(std::make_error_code(std::errc::io_error));
        auto user = rows.first();
        if (!user) co_return std::optional<user_profile>{};

        // 3) 回填失败不影响主响应。
        if (auto cache_conn = co_await redis_pool_.async_get_connection(io); cache_conn) {
            const auto payload = orm::to_json(*user).dump();
            (void)co_await (*cache_conn)->cmd({"SET", key, payload, "EX", "300"});
        }
        co_return user;
    }

private:
    mysql::sharded_connection_pool& mysql_pool_;
    redis::sharded_connection_pool& redis_pool_;
};

auto make_user_handler(user_service& users) -> http::handler_fn {
    return [&users](http::request_context& request) -> cn::task<void> {
        std::int64_t id{};
        const auto text = request.param("id");
        const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), id);
        if (error != std::errc{} || end != text.data() + text.size() || id <= 0) {
            request.json(http::status::bad_request, R"({"code":400,"message":"invalid user id"})");
            co_return;
        }
        auto found = co_await users.get_by_id(request.io_ctx(), id);
        if (!found) {
            request.json(http::status::service_unavailable, R"({"code":503,"message":"database unavailable"})");
            co_return;
        }
        if (!*found) {
            request.json(http::status::not_found, R"({"code":404,"message":"user not found"})");
            co_return;
        }
        request.json(http::status::ok, orm::to_json(**found).dump());
    };
}

auto main() -> int {
    cn::net_init net; // Windows 下由 RAII 初始化 Winsock。
    const auto workers = std::max(4u, std::thread::hardware_concurrency());
    cn::server_context runtime(workers, workers);
    const auto worker_ios = runtime.worker_ios();

    mysql::sharded_connection_pool mysql_pool(worker_ios, {
        .host = "127.0.0.1", .username = "app", .password = "replace-me",
        .database = "app", .initial_size = 4, .max_size = workers * 4,
    });
    redis::sharded_connection_pool redis_pool(worker_ios, {
        .host = "127.0.0.1", .initial_size = 4, .max_size = workers * 8,
    });
    cn::spawn(runtime.accept_io(), mysql_pool.async_run());
    cn::spawn(runtime.accept_io(), redis_pool.async_run());

    user_service users(mysql_pool, redis_pool);
    http::router router;
    router.get("/api/v1/users/:id", make_user_handler(users));
    http::server server(runtime);
    if (const auto listening = server.listen("0.0.0.0", 8080); !listening) return 1;
    server.set_router(std::move(router));
    cn::spawn(runtime.accept_io(), server.run());
    runtime.run();
    return 0;
}
```

## 构建

```bash
cmake -S . -B build -DCNETMOD_ENABLE_HTTP=ON \
  -DCNETMOD_ENABLE_MYSQL=ON -DCNETMOD_ENABLE_REDIS=ON
cmake --build build --config Release --target your_target
```

## 上线建议

连接池上限应受数据库的真实连接预算约束，而不是盲目随 CPU 核数增长。热点 key 应进一步加 single-flight/请求合并，避免过期瞬间并发穿透；写路径应在数据库事务提交后删除或更新 Redis key，保证读写一致性。
