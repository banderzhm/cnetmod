module cnetmod.application.amqp10;

#ifdef CNETMOD_HAS_PROTOCOL_AMQP10
import std;
import cnetmod.core.log;

namespace cnetmod::application {

amqp10_service::amqp10_service(io_context& context,
    amqp10::client_options options)
    : options_(std::move(options)), client_(context)
{
}

auto amqp10_service::client() noexcept -> amqp10::client&
{
    return client_;
}

auto amqp10_service::start() -> task<std::expected<void, std::error_code>>
{
    if (started_)
        co_return {};
    operation_cancel_.reset();
    auto connected = co_await client_.connect(options_, operation_cancel_);
    if (!connected)
    {
        logger::error("AMQP 1.0 startup failed: {}",
            connected.error().message);
        co_return std::unexpected(
            std::make_error_code(std::errc::connection_refused));
    }
    started_ = true;
    co_return {};
}

auto amqp10_service::stop() -> task<std::expected<void, std::error_code>>
{
    if (!started_)
        co_return {};
    operation_cancel_.reset();
    auto closed = co_await client_.close(operation_cancel_);
    started_ = false;
    if (!closed)
    {
        logger::warn("AMQP 1.0 shutdown failed: {}", closed.error().message);
        co_return std::unexpected(std::make_error_code(std::errc::io_error));
    }
    co_return {};
}

auto install_amqp10(http_application& application,
    amqp10::client_options options) -> amqp10_service&
{
    if (application.services().find<amqp10_service>())
        throw std::logic_error("AMQP 1.0 is already installed");
    auto service = std::make_shared<amqp10_service>(application.context(),
        std::move(options));
    auto& result = *service;
    application.services().add<amqp10_service>(service);
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
