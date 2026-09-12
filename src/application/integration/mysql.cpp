module cnetmod.application.mysql;

#ifdef CNETMOD_HAS_PROTOCOL_MYSQL
import std;
import cnetmod.coro.spawn;

namespace cnetmod::application {

mysql_service::mysql_service(io_context& context, mysql::pool_params options)
    : context_(context), pool_(context, std::move(options))
{
}

auto mysql_service::pool() noexcept -> mysql::connection_pool&
{
    return pool_;
}

auto mysql_service::start() -> task<std::expected<void, std::error_code>>
{
    if (!started_)
    {
        started_ = true;
        spawn(context_, pool_.async_run());
    }
    co_return {};
}

auto mysql_service::stop() -> task<std::expected<void, std::error_code>>
{
    if (started_)
    {
        started_ = false;
        co_await pool_.cancel();
    }
    co_return {};
}

auto install_mysql(http_application& application, mysql::pool_params options)
    -> mysql_service&
{
    if (application.services().find<mysql_service>())
        throw std::logic_error("MySQL is already installed");
    auto service = std::make_shared<mysql_service>(application.context(),
        std::move(options));
    auto& result = *service;
    application.services().add<mysql_service>(service);
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
