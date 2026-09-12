module cnetmod.application.amqp091;

#ifdef CNETMOD_HAS_PROTOCOL_AMQP091
import std;
import cnetmod.core.log;
import cnetmod.coro.spawn;

namespace cnetmod::application {

amqp091_service::amqp091_service(io_context& context,
    amqp091::connection_options options)
    : context_(context), options_(std::move(options)), client_(context)
{
}

auto amqp091_service::client() noexcept -> amqp091::amqp091_client&
{
    return client_;
}

auto amqp091_service::start() -> task<std::expected<void, std::error_code>>
{
    if (started_)
        co_return {};
    run_cancel_.reset();
    auto connected = co_await client_.async_connect(options_, run_cancel_);
    if (!connected)
    {
        logger::error("AMQP 0-9-1 startup failed: {}",
            connected.error().message);
        co_return std::unexpected(
            std::make_error_code(std::errc::connection_refused));
    }
    started_ = true;
    spawn(context_, run());
    co_return {};
}

auto amqp091_service::stop() -> task<std::expected<void, std::error_code>>
{
    if (!started_)
        co_return {};
    run_cancel_.cancel();
    auto closed = co_await client_.async_close();
    started_ = false;
    if (!closed)
    {
        logger::warn("AMQP 0-9-1 shutdown failed: {}",
            closed.error().message);
        co_return std::unexpected(std::make_error_code(std::errc::io_error));
    }
    co_return {};
}

auto amqp091_service::run() -> task<void>
{
    auto result = co_await client_.async_run(run_cancel_);
    if (!result && !run_cancel_.is_cancelled())
        logger::error("AMQP 0-9-1 connection pump failed: {}",
            result.error().message);
}

auto install_amqp091(http_application& application,
    amqp091::connection_options options) -> amqp091_service&
{
    if (application.services().find<amqp091_service>())
        throw std::logic_error("AMQP 0-9-1 is already installed");
    auto service = std::make_shared<amqp091_service>(application.context(),
        std::move(options));
    auto& result = *service;
    application.services().add<amqp091_service>(service);
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
