module;

#include <cnetmod/config.hpp>

/// Application-managed Redis connection pool.
export module cnetmod.application.redis;

#ifdef CNETMOD_HAS_PROTOCOL_REDIS
import std;
import cnetmod.application.http;
import cnetmod.coro.task;
import cnetmod.io.io_context;
import cnetmod.protocol.redis;

namespace cnetmod::application {

export class redis_service final
{
public:
    redis_service(io_context& context, redis::pool_params options);

    [[nodiscard]] auto pool() noexcept -> redis::connection_pool&;
    [[nodiscard]] auto start() -> task<std::expected<void, std::error_code>>;
    [[nodiscard]] auto stop() -> task<std::expected<void, std::error_code>>;

private:
    io_context& context_;
    redis::connection_pool pool_;
    bool started_ = false;
};

/// Register one Redis pool and bind its lifetime to the application.
export auto install_redis(http_application& application,
    redis::pool_params options = {}) -> redis_service&;

} // namespace cnetmod::application
#endif
