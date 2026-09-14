module cnetmod.application.openai;

#ifdef CNETMOD_HAS_PROTOCOL_OPENAI
import std;
import cnetmod.observability.openai;

namespace cnetmod::application {

openai_service::openai_service(io_context& io,
    observability::telemetry_hub& telemetry, openai::connect_options options,
    std::string instance, service_requirement requirement,
    recovery_policy recovery)
    : client_(io),
      options_(std::move(options)),
      instance_(std::move(instance)),
      requirement_(requirement),
      recovery_(recovery)
{
    auto sink = telemetry.spans();
    if (sink || telemetry.records_metrics())
    {
        openai::telemetry_options settings;
        settings.record_metrics = telemetry.records_metrics();
        telemetry_listener_.emplace(telemetry.measurements(), std::move(sink),
            std::move(settings));
    }
}

auto openai_service::client() noexcept -> openai::client&
{
    return client_;
}

auto openai_service::telemetry_listener() noexcept
    -> openai::telemetry_listener*
{
    return telemetry_listener_ ? &*telemetry_listener_ : nullptr;
}

auto openai_service::run_configuration(openai::run_config configuration)
    -> openai::run_config
{
    auto* listener = telemetry_listener();
    if (listener && std::ranges::find(configuration.listeners, listener) == configuration.listeners.end())
        configuration.listeners.push_back(listener);
    return configuration;
}

auto openai_service::key() const -> service_key
{
    return {"openai", instance_};
}

auto openai_service::requirement() const noexcept -> service_requirement
{
    return requirement_;
}

auto openai_service::recovery() const noexcept -> recovery_policy
{
    return recovery_;
}

auto openai_service::start(service_context& context)
    -> task<std::expected<void, std::error_code>>
{
    (void)context;
    if (client_.is_connected())
        co_return {};
    auto connected = co_await client_.connect(options_);
    if (!connected)
        co_return std::unexpected(
            std::make_error_code(std::errc::connection_refused));
    co_return {};
}

auto openai_service::stop(service_context& context)
    -> task<std::expected<void, std::error_code>>
{
    (void)context;
    client_.close();
    co_return {};
}

auto openai_service::probe(service_context& context) -> task<health_report>
{
    (void)context;
    co_return health_report{
        .status = client_.is_connected() ? service_health::up
                                         : service_health::down,
        .message = client_.is_connected() ? "openai connection available"
                                          : "openai connection unavailable",
    };
}

auto auto_configure_openai(const configured_service& configuration,
    auto_configuration_context& context)
    -> std::expected<void, std::error_code>
{
    if (!properties_are_known(configuration.properties,
            {"base_url", "api_key", "tls_verify", "timeout_seconds"}))
        return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
    openai::connect_options options;
    if (!integer_property_in_range(configuration.properties, "timeout_seconds", 1,
            std::numeric_limits<int>::max()))
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    try
    {
        options.api_base = configuration.properties.value("base_url",
            options.api_base);
        options.api_key = configuration.properties.value("api_key",
            options.api_key);
        options.tls_verify = configuration.properties.value("tls_verify",
            options.tls_verify);
        options.timeout_seconds = configuration.properties.value(
            "timeout_seconds", options.timeout_seconds);
    }
    catch (...)
    {
        return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
    }
    if (options.api_key.empty())
        return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
    auto service = std::make_shared<openai_service>(context.io,
        context.telemetry,
        std::move(options), configuration.instance,
        configuration.requirement, configuration.recovery);
    return context.services.add_managed_named<openai_service>(
        configuration.instance, std::move(service));
}

} // namespace cnetmod::application
#endif
