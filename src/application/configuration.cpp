module cnetmod.application.configuration;

import std;

namespace cnetmod::application {
namespace {

    auto environment(std::string_view name) -> std::optional<std::string>
    {
        if (const auto* value = std::getenv(std::string{name}.c_str());
            value && *value != '\0')
            return std::string{value};
        return std::nullopt;
    }

    auto environment_port(std::string_view name) -> std::optional<std::uint16_t>
    {
        const auto value = environment(name);
        if (!value)
            return std::nullopt;
        unsigned int parsed{};
        const auto [end, error] = std::from_chars(
            value->data(), value->data() + value->size(), parsed);
        if (error != std::errc{} || end != value->data() + value->size() ||
            parsed == 0U || parsed > std::numeric_limits<std::uint16_t>::max())
            return std::nullopt;
        return static_cast<std::uint16_t>(parsed);
    }

    auto trace_endpoint_from_base(std::string endpoint) -> std::string
    {
        while (endpoint.ends_with('/'))
            endpoint.pop_back();
        return endpoint + "/v1/traces";
    }

    void apply_attributes(std::string_view encoded,
        std::map<std::string, std::string, std::less<>>& destination)
    {
        for (std::size_t begin = 0; begin < encoded.size();)
        {
            const auto comma = encoded.find(',', begin);
            const auto end = comma == std::string_view::npos
                ? encoded.size()
                : comma;
            const auto entry = encoded.substr(begin, end - begin);
            const auto equals = entry.find('=');
            if (equals != std::string_view::npos && equals > 0U)
                destination.insert_or_assign(std::string{entry.substr(0, equals)},
                    std::string{entry.substr(equals + 1U)});
            if (comma == std::string_view::npos)
                break;
            begin = comma + 1U;
        }
    }

} // namespace

auto load_application_options(application_options defaults)
    -> application_options
{
    if (auto value = environment("CNETMOD_APPLICATION_NAME"))
        defaults.name = std::move(*value);
    if (auto value = environment("CNETMOD_HTTP_ADDRESS"))
        defaults.http.address = std::move(*value);
    if (auto value = environment_port("CNETMOD_HTTP_PORT"))
        defaults.http.port = *value;
    if (auto value = environment("OTEL_EXPORTER_OTLP_TRACES_ENDPOINT"))
        defaults.observability.otlp.endpoint = std::move(*value);
    else if (auto value = environment("OTEL_EXPORTER_OTLP_ENDPOINT"))
        defaults.observability.otlp.endpoint =
            trace_endpoint_from_base(std::move(*value));
    if (auto value = environment("OTEL_SERVICE_NAME"))
        defaults.observability.otlp.service_name = std::move(*value);
    if (auto value = environment("OTEL_SERVICE_VERSION"))
        defaults.observability.otlp.service_version = std::move(*value);
    if (auto value = environment("OTEL_RESOURCE_ATTRIBUTES"))
        apply_attributes(*value,
            defaults.observability.otlp.resource_attributes);
    if (auto value = environment("OTEL_EXPORTER_OTLP_HEADERS"))
        apply_attributes(*value, defaults.observability.otlp.headers);
    return defaults;
}

} // namespace cnetmod::application
