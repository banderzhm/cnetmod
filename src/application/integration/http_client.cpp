module cnetmod.application.http_client;

import std;

namespace cnetmod::application {

http_client_service::http_client_service(io_context& io,
    observability::telemetry_hub& telemetry, http::client_options options,
    std::string instance, service_requirement requirement)
    : raw_client_(io, std::move(options)), client_(raw_client_, telemetry.spans(), telemetry.measurements()), instance_(std::move(instance)), requirement_(requirement)
{
}

auto http_client_service::client() noexcept
    -> observability::instrumented_http_client&
{
    return client_;
}

auto http_client_service::raw_client() noexcept -> http::client&
{
    return raw_client_;
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
    raw_client_.close();
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
            configuration.properties.value("connect_timeout_ms",
                options.connect_timeout.count())};
        options.request_timeout = std::chrono::milliseconds{
            configuration.properties.value("request_timeout_ms",
                options.request_timeout.count())};
        options.verify_peer = configuration.properties.value("tls_verify",
            options.verify_peer);
    }
    catch (...)
    {
        return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
    }
    auto service = std::make_shared<http_client_service>(context.io,
        context.telemetry, std::move(options), configuration.instance,
        configuration.requirement);
    return context.services.add_managed_named<http_client_service>(
        configuration.instance, std::move(service));
}

} // namespace cnetmod::application
