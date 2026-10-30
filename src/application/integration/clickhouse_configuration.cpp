module cnetmod.application.clickhouse;
#ifdef CNETMOD_HAS_PROTOCOL_CLICKHOUSE
import std;
import cnetmod.json;

namespace cnetmod::application {
auto auto_configure_clickhouse(const configured_service& configuration, auto_configuration_context& context)
    -> std::expected<void, std::error_code>
{
    const auto& value = configuration.properties;
    if (!properties_are_known(value, {"host", "port", "username", "password", "database", "minimum_size", "maximum_size", "tls", "ca_file", "compression", "connect_timeout_ms", "operation_timeout_ms"}) ||
        !integer_property_in_range(value, "port", 1, 65535) ||
        !integer_property_in_range(value, "connect_timeout_ms", 1, 3600000) ||
        !integer_property_in_range(value, "operation_timeout_ms", 1, 3600000))
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    clickhouse::connection_pool_options options;
    if (!pool_size_properties_are_valid(value, options.minimum_connections, options.maximum_connections))
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    try
    {
        options.connection.host = cnetmod::json::value_or(value, "host", options.connection.host);
        options.connection.user = cnetmod::json::value_or(value, "username", options.connection.user);
        options.connection.password = cnetmod::json::value_or(value, "password", options.connection.password);
        options.connection.database = cnetmod::json::value_or(value, "database", options.connection.database);
        options.connection.port = cnetmod::json::value_or(value, "port", options.connection.port);
        options.connection.tls = cnetmod::json::value_or(value, "tls", false);
        options.connection.ca_file = cnetmod::json::value_or(value, "ca_file", std::string{});
        options.connection.connect_timeout = std::chrono::milliseconds(cnetmod::json::value_or(value, "connect_timeout_ms", 5000));
        options.connection.operation_timeout = std::chrono::milliseconds(cnetmod::json::value_or(value, "operation_timeout_ms", 30000));
        options.minimum_connections = cnetmod::json::value_or(value, "minimum_size", options.minimum_connections);
        options.maximum_connections = cnetmod::json::value_or(value, "maximum_size", options.maximum_connections);
        const auto compression = cnetmod::json::value_or(value, "compression", std::string{"lz4"});
        if (compression == "lz4")
            options.connection.compression = clickhouse::compression_mode::lz4;
        else if (compression == "zstd")
            options.connection.compression = clickhouse::compression_mode::zstd;
        else if (compression == "checksum")
            options.connection.compression = clickhouse::compression_mode::checksum;
        else if (compression == "none")
            options.connection.compression = clickhouse::compression_mode::none;
        else
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        if (options.connection.host.empty() || options.maximum_connections > 4096 || options.maximum_connections < context.event_loops.size())
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        auto service = context.event_loops.size() > 1 ? std::make_shared<clickhouse_service>(context.event_loops, options, configuration.instance, configuration.requirement, configuration.recovery) : std::make_shared<clickhouse_service>(context.io, options, configuration.instance, configuration.requirement, configuration.recovery);
        return context.services.add_managed_named<clickhouse_service>(configuration.instance, std::move(service));
    }
    catch (...)
    {
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }
}
} // namespace cnetmod::application
#endif
