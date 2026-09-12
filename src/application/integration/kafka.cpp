module cnetmod.application.kafka;

#ifdef CNETMOD_HAS_PROTOCOL_KAFKA
import std;
import cnetmod.core.log;

namespace cnetmod::application {

kafka_service::kafka_service(io_context& context, kafka::client_options options)
    : client_(context, std::move(options))
{
}

auto kafka_service::client() noexcept -> kafka::client_facade&
{
    return client_;
}

auto kafka_service::start() -> task<std::expected<void, std::error_code>>
{
    if (started_)
        co_return {};
    auto connected = co_await client_.connect();
    if (!connected)
    {
        logger::error("Kafka startup failed: {}", connected.error().message);
        co_return std::unexpected(
            std::make_error_code(std::errc::connection_refused));
    }
    started_ = true;
    co_return {};
}

auto kafka_service::stop() -> task<std::expected<void, std::error_code>>
{
    if (started_)
    {
        client_.close();
        started_ = false;
    }
    co_return {};
}

auto install_kafka(http_application& application, kafka::client_options options)
    -> kafka_service&
{
    if (application.services().find<kafka_service>())
        throw std::logic_error("Kafka is already installed");
    if (options.bootstrap_servers.empty())
        options.bootstrap_servers.push_back({});
    auto service = std::make_shared<kafka_service>(application.context(),
        std::move(options));
    auto& result = *service;
    application.services().add<kafka_service>(service);
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
