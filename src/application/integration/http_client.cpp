module cnetmod.application.http_client;

import std;
import cnetmod.json;

namespace cnetmod::application {

struct http_client_service::client_shard
{
    client_shard(io_context& event_loop,
        observability::telemetry_hub& telemetry,
        const http::client_options& options)
        : event_loop(&event_loop), raw(event_loop, options),
          observed(raw, telemetry.spans(), telemetry.measurements())
    {
    }

    io_context* event_loop;
    http::client raw;
    observability::instrumented_http_client observed;
};

http_client_service::~http_client_service() = default;

http_client_service::http_client_service(io_context& io,
    observability::telemetry_hub& telemetry, http::client_options options,
    std::string instance, service_requirement requirement)
    : instance_(std::move(instance)), requirement_(requirement)
{
    clients_.push_back(std::make_unique<client_shard>(
        io, telemetry, options));
}

http_client_service::http_client_service(
    std::span<io_context* const> event_loops,
    observability::telemetry_hub& telemetry, http::client_options options,
    std::string instance, service_requirement requirement)
    : instance_(std::move(instance)), requirement_(requirement)
{
    clients_.reserve(event_loops.size());
    for (auto* event_loop : event_loops)
        clients_.push_back(std::make_unique<client_shard>(
            *event_loop, telemetry, options));
}

auto http_client_service::current_shard() -> client_shard&
{
    auto* current = io_context::current();
    for (auto& shard : clients_)
        if (shard->event_loop == current)
            return *shard;
    throw std::logic_error{
        "HTTP client requested outside an owning event loop"};
}

auto http_client_service::client()
    -> observability::instrumented_http_client&
{
    return current_shard().observed;
}

auto http_client_service::raw_client() -> http::client&
{
    return current_shard().raw;
}

auto http_client_service::key() const -> service_key
{
    return {"http_client", instance_};
}

auto http_client_service::requirement() const noexcept -> service_requirement
{
    return requirement_;
}

auto http_client_service::start(service_context& context)
    -> task<std::expected<void, std::error_code>>
{
    (void)context;
    started_ = true;
    co_return {};
}

auto http_client_service::stop(service_context& context)
    -> task<std::expected<void, std::error_code>>
{
    (void)context;
    for (auto& shard : clients_)
    {
        auto close = [&raw = shard->raw]() -> task<void>
        {
            raw.close();
            co_return;
        };
        co_await resume_on(context.io,
            starts_on(*shard->event_loop, close()));
    }
    started_ = false;
    co_return {};
}

auto http_client_service::probe(service_context& context) -> task<health_report>
{
    (void)context;
    co_return health_report{
        .status = started_ ? service_health::up : service_health::stopped,
        .message = started_ ? "http client available" : "http client stopped",
    };
}

auto auto_configure_http_client(const configured_service& configuration,
    auto_configuration_context& context)
    -> std::expected<void, std::error_code>
{
    if (!properties_are_known(configuration.properties,
            {"connect_timeout_ms", "request_timeout_ms", "tls_verify"}))
        return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
    http::client_options options;
    constexpr auto maximum_timeout = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::duration::max() / 2).count();
    if (!integer_property_in_range(configuration.properties, "connect_timeout_ms", 1, maximum_timeout) ||
        !integer_property_in_range(configuration.properties, "request_timeout_ms", 1, maximum_timeout))
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    try
    {
        options.connect_timeout = std::chrono::milliseconds{
            cnetmod::json::value_or(configuration.properties, "connect_timeout_ms",
                options.connect_timeout.count())};
        options.request_timeout = std::chrono::milliseconds{
            cnetmod::json::value_or(configuration.properties, "request_timeout_ms",
                options.request_timeout.count())};
        options.verify_peer = cnetmod::json::value_or(configuration.properties, "tls_verify",
            options.verify_peer);
    }
    catch (...)
    {
        return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
    }
    std::shared_ptr<http_client_service> service;
    if (context.event_loops.size() > 1)
        service = std::make_shared<http_client_service>(context.event_loops,
            context.telemetry, std::move(options), configuration.instance,
            configuration.requirement);
    else
        service = std::make_shared<http_client_service>(context.io,
            context.telemetry, std::move(options), configuration.instance,
            configuration.requirement);
    return context.services.add_managed_named<http_client_service>(
        configuration.instance, std::move(service));
}

} // namespace cnetmod::application
