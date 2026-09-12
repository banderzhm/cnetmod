module cnetmod.application.redis;

#ifdef CNETMOD_HAS_PROTOCOL_REDIS
import std;
import cnetmod.coro.spawn;

namespace cnetmod::application {

redis_service::redis_service(io_context& context, redis::pool_params options)
    : context_(context), pool_(context, std::move(options))
{
}

auto redis_service::pool() noexcept -> redis::connection_pool&
{
    return pool_;
}

auto redis_service::start() -> task<std::expected<void, std::error_code>>
{
    if (!started_)
    {
        started_ = true;
        spawn(context_, pool_.async_run());
    }
    co_return {};
}

auto redis_service::stop() -> task<std::expected<void, std::error_code>>
{
    if (started_)
    {
        started_ = false;
        co_await pool_.cancel();
    }
    co_return {};
}

auto install_redis(http_application& application, redis::pool_params options)
    -> redis_service&
{
    if (application.services().find<redis_service>())
        throw std::logic_error("Redis is already installed");
    auto service = std::make_shared<redis_service>(application.context(),
        std::move(options));
    auto& result = *service;
    application.services().add<redis_service>(service);
    application.lifecycle().on_start([service]
        {
            return service->start();
        });
    application.lifecycle().on_stop([service]
        {
            return service->stop();
        });
    return result;
}

} // namespace cnetmod::application
#endif
