module cnetmod.integration.http.redis_cache;

import std;
import cnetmod.coro.task;
import cnetmod.protocol.redis;

namespace cnetmod::cache {

redis_cache::redis_cache(redis::client& client,
    redis_cache_options opts) noexcept
    : client_(client), opts_(std::move(opts)) {}

auto redis_cache::full_key(std::string_view key) const -> std::string
{
    return opts_.key_prefix + std::string(key);
}

auto redis_cache::get(std::string_view key)
    -> task<std::optional<std::string>>
{
    redis::request req;
    req.push("GET", full_key(key));
    auto result = co_await client_.exec(req);
    if (!result || result->empty() || result->front().is_null() ||
        result->front().is_error())
        co_return std::nullopt;
    co_return std::string(redis::first_value(*result));
}

auto redis_cache::set(std::string_view key, std::string_view value,
    std::chrono::seconds ttl) -> task<bool>
{
    redis::request req;
    if (ttl.count() > 0)
        req.push("SET", full_key(key), std::string(value), std::string("EX"),
            std::to_string(ttl.count()));
    else
        req.push("SET", full_key(key), std::string(value));
    auto result = co_await client_.exec(req);
    co_return result.has_value() && redis::is_ok(*result);
}

auto redis_cache::del(std::string_view key) -> task<bool>
{
    redis::request req;
    req.push("DEL", full_key(key));
    auto result = co_await client_.exec(req);
    co_return result && !result->empty() && redis::first_value(*result) != "0";
}

auto redis_cache::exists(std::string_view key) -> task<bool>
{
    redis::request req;
    req.push("EXISTS", full_key(key));
    auto result = co_await client_.exec(req);
    co_return result && !result->empty() && redis::first_value(*result) != "0";
}

} // namespace cnetmod::cache
