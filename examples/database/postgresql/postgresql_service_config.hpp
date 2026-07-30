#pragma once

namespace postgresql_example {

inline auto environment(std::string_view name, std::string fallback = {}) -> std::string
{
    if (const char* value = std::getenv(std::string(name).c_str()))
        return value;
    return fallback;
}

inline auto environment_flag(std::string_view name, bool fallback = false) -> bool
{
    auto value = environment(name);
    if (value.empty())
        return fallback;
    std::ranges::transform(value, value.begin(), [](unsigned char character)
        {
            return static_cast<char>(std::tolower(character));
        });
    return value == "1" || value == "true" || value == "yes" || value == "on";
}

inline auto parse_port(std::string_view text) -> std::uint16_t
{
    const auto value = std::stoul(std::string(text));
    if (value == 0 || value > 65535)
        throw std::out_of_range("network port must be between 1 and 65535");
    return static_cast<std::uint16_t>(value);
}

struct database_endpoint
{
    std::string host;
    std::uint16_t port{5432};

    [[nodiscard]] auto display_name() const -> std::string
    {
        return std::format("{}:{}", host, port);
    }
};

inline auto parse_database_endpoint(std::string_view text) -> database_endpoint
{
    if (text.empty())
        throw std::invalid_argument("PostgreSQL endpoint is empty");
    database_endpoint endpoint;
    if (text.front() == '[')
    {
        const auto closing = text.find(']');
        if (closing == std::string_view::npos)
            throw std::invalid_argument("invalid bracketed PostgreSQL endpoint");
        endpoint.host = text.substr(1, closing - 1);
        if (closing + 1 < text.size())
        {
            if (text[closing + 1] != ':')
                throw std::invalid_argument("invalid PostgreSQL endpoint port");
            endpoint.port = parse_port(text.substr(closing + 2));
        }
    }
    else
    {
        const auto separator = text.rfind(':');
        if (separator == std::string_view::npos || text.find(':') != separator)
            endpoint.host = text;
        else
        {
            endpoint.host = text.substr(0, separator);
            endpoint.port = parse_port(text.substr(separator + 1));
        }
    }
    if (endpoint.host.empty() || endpoint.port == 0)
        throw std::invalid_argument("invalid PostgreSQL endpoint");
    return endpoint;
}

struct service_config
{
    std::vector<database_endpoint> endpoints;
    std::string username;
    std::string password;
    std::string database;
    std::string ca_file;
    std::string http_host;
    std::uint16_t http_port{};
    std::size_t minimum_pool_connections{};
    std::size_t maximum_pool_connections{};
    std::size_t failover_attempts{};
    std::chrono::milliseconds acquire_timeout{};
    std::chrono::milliseconds retry_backoff{};
    std::chrono::milliseconds statement_timeout{};
    std::chrono::milliseconds lock_timeout{};
    std::chrono::milliseconds idle_transaction_timeout{};
    std::chrono::milliseconds shutdown_grace{};
    bool enable_remote_shutdown{};

    static auto from_environment() -> service_config
    {
        service_config value;
        auto endpoint_list = environment("CNETMOD_POSTGRESQL_ENDPOINTS");
        if (endpoint_list.empty())
        {
            endpoint_list = std::format("{}:{}",
                environment("CNETMOD_POSTGRESQL_HOST", "127.0.0.1"),
                environment("CNETMOD_POSTGRESQL_PORT", "5432"));
        }
        for (auto endpoint : std::views::split(endpoint_list, ','))
        {
            std::string text(endpoint.begin(), endpoint.end());
            if (!text.empty())
                value.endpoints.push_back(parse_database_endpoint(text));
        }
        if (value.endpoints.empty())
            throw std::invalid_argument("at least one PostgreSQL endpoint is required");

        value.username = environment("CNETMOD_POSTGRESQL_USERNAME", "cnetmod");
        value.password = environment("CNETMOD_POSTGRESQL_PASSWORD");
        value.database = environment("CNETMOD_POSTGRESQL_DATABASE", "cnetmod");
        value.ca_file = environment("CNETMOD_POSTGRESQL_CA_FILE");
        value.http_host = environment("CNETMOD_POSTGRESQL_HTTP_HOST", "0.0.0.0");
        value.http_port = parse_port(
            environment("CNETMOD_POSTGRESQL_HTTP_PORT", "18080"));
        value.minimum_pool_connections = std::clamp<std::size_t>(std::stoull(
                                                                     environment("CNETMOD_POSTGRESQL_POOL_MIN", "2")),
            1, 64);
        value.maximum_pool_connections = std::clamp<std::size_t>(std::stoull(
                                                                     environment("CNETMOD_POSTGRESQL_POOL_MAX", "32")),
            value.minimum_pool_connections, 256);
        value.failover_attempts = std::max<std::size_t>(1, std::stoull(environment("CNETMOD_POSTGRESQL_FAILOVER_ATTEMPTS", "3")));
        value.acquire_timeout = std::chrono::milliseconds(std::stoull(
            environment("CNETMOD_POSTGRESQL_ACQUIRE_TIMEOUT_MS", "2000")));
        value.retry_backoff = std::chrono::milliseconds(std::stoull(
            environment("CNETMOD_POSTGRESQL_RETRY_BACKOFF_MS", "100")));
        value.statement_timeout = std::chrono::milliseconds(std::stoull(
            environment("CNETMOD_POSTGRESQL_STATEMENT_TIMEOUT_MS", "5000")));
        value.lock_timeout = std::chrono::milliseconds(std::stoull(
            environment("CNETMOD_POSTGRESQL_LOCK_TIMEOUT_MS", "2000")));
        value.idle_transaction_timeout = std::chrono::milliseconds(std::stoull(
            environment("CNETMOD_POSTGRESQL_IDLE_TRANSACTION_TIMEOUT_MS", "10000")));
        value.shutdown_grace = std::chrono::milliseconds(std::stoull(
            environment("CNETMOD_POSTGRESQL_SHUTDOWN_GRACE_MS", "30000")));
        value.enable_remote_shutdown = environment_flag(
            "CNETMOD_EXAMPLE_ENABLE_SHUTDOWN", false);
        return value;
    }

    [[nodiscard]] auto connection_options(std::size_t endpoint_index,
        std::string application_name) const -> cnetmod::postgresql::connection_options
    {
        const auto& endpoint = endpoints.at(endpoint_index);
        cnetmod::postgresql::connection_options options;
        options.host = endpoint.host;
        options.port = endpoint.port;
        options.username = username;
        options.password = password;
        options.database = database;
        options.application_name = std::move(application_name);
        options.connect_timeout = std::chrono::seconds{10};
        options.maximum_connect_attempts = failover_attempts;
        options.connect_retry_backoff = retry_backoff;
        options.maximum_message_size = 16U * 1024U * 1024U;
        options.maximum_row_count = 10000;
        options.tls = ca_file.empty() ? cnetmod::postgresql::tls_mode::prefer
                                      : cnetmod::postgresql::tls_mode::verify_full;
        options.tls_ca_file = ca_file;
        options.startup_parameters["statement_timeout"] =
            std::to_string(statement_timeout.count());
        options.startup_parameters["lock_timeout"] =
            std::to_string(lock_timeout.count());
        options.startup_parameters["idle_in_transaction_session_timeout"] =
            std::to_string(idle_transaction_timeout.count());
        return options;
    }
};

} // namespace postgresql_example
