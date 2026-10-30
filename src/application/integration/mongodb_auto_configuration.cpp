module cnetmod.application.mongodb;
#ifdef CNETMOD_HAS_PROTOCOL_MONGODB
import std;
import cnetmod.json;

namespace cnetmod::application {
auto auto_configure_mongodb(const configured_service& configuration,
    auto_configuration_context& context)
    -> std::expected<void, std::error_code>
{
    const auto& properties = configuration.properties;
    const auto* uri = cnetmod::json::find(properties, "uri");
    if (uri)
    {
        // URI and legacy addressing are separate configurations; reject mixed
        // inputs so an ignored credential or host cannot silently take effect.
        if (!properties_are_known(properties, {"uri", "minimum_size", "maximum_size", "heartbeat_frequency_ms", "tls_ca_file", "tls_cert_file", "tls_key_file", "tls_sni"}) || !uri->is_string())
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        mongodb_service_options options;
        options.uri = uri->get<std::string>();
        auto parsed = mongodb::parse_connection_string(options.uri);
        if (!parsed || !pool_size_properties_are_valid(properties, parsed->topology.per_server_pool.minimum_size, parsed->topology.per_server_pool.maximum_size) ||
            !integer_property_in_range(properties, "heartbeat_frequency_ms", 500, 86400000))
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        options.heartbeat_frequency = std::chrono::milliseconds{cnetmod::json::value_or(properties, "heartbeat_frequency_ms", 10000)};
        if (cnetmod::json::find(properties, "minimum_size"))
            options.minimum_size = cnetmod::json::value_or(properties, "minimum_size", std::size_t{});
        if (cnetmod::json::find(properties, "maximum_size"))
            options.maximum_size = cnetmod::json::value_or(properties, "maximum_size", std::size_t{});
        mongodb::connection_options tls;
        const std::array fields{std::pair{std::string_view{"tls_ca_file"}, &tls.tls_ca_file},
            std::pair{std::string_view{"tls_cert_file"}, &tls.tls_cert_file},
            std::pair{std::string_view{"tls_key_file"}, &tls.tls_key_file},
            std::pair{std::string_view{"tls_sni"}, &tls.tls_sni}};
        for (const auto& [name, target] : fields)
        {
            if (const auto* value = cnetmod::json::find(properties, name))
            {
                if (!value->is_string())
                    return std::unexpected(std::make_error_code(std::errc::invalid_argument));
                *target = value->get<std::string>();
            }
        }
        options.configure_connection = [tls = std::move(tls)](mongodb::connection_options& connection) -> mongodb::result<void>
        {
            connection.tls_ca_file = tls.tls_ca_file;
            connection.tls_cert_file = tls.tls_cert_file;
            connection.tls_key_file = tls.tls_key_file;
            connection.tls_sni = tls.tls_sni;
            return {};
        };
        auto service = std::make_shared<mongodb_service>(std::move(options), context.io,
            configuration.instance, configuration.requirement, configuration.recovery);
        return context.services.add_managed_named<mongodb_service>(configuration.instance, std::move(service));
    }
    if (!properties_are_known(configuration.properties, {"host", "port", "username", "password", "database", "minimum_size", "maximum_size"}))
        return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
    if (!integer_property_in_range(configuration.properties, "port", 1, 65535))
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    mongodb::connection_pool_options options;
    if (!pool_size_properties_are_valid(configuration.properties, options.minimum_size, options.maximum_size))
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    try
    {
        const auto& value = configuration.properties;
        options.connection.host = cnetmod::json::value_or(value, "host", options.connection.host);
        options.connection.port = cnetmod::json::value_or(value, "port", options.connection.port);
        options.connection.username = cnetmod::json::value_or(value, "username",
            options.connection.username);
        options.connection.password = cnetmod::json::value_or(value, "password",
            options.connection.password);
        options.connection.database = cnetmod::json::value_or(value, "database",
            options.connection.database);
        options.minimum_size = cnetmod::json::value_or(value, "minimum_size", options.minimum_size);
        options.maximum_size = cnetmod::json::value_or(value, "maximum_size", options.maximum_size);
    }
    catch (...)
    {
        return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));
    }
    auto service = std::make_shared<mongodb_service>(context.io,
        std::move(options), configuration.instance,
        configuration.requirement, configuration.recovery);
    return context.services.add_managed_named<mongodb_service>(
        configuration.instance, std::move(service));
}

} // namespace cnetmod::application
#endif
