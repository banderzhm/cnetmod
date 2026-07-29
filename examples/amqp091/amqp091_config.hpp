#pragma once

namespace amqp091_example {

inline auto environment(std::string_view name, std::string fallback = {}) -> std::string
{
    if (const char* value = std::getenv(std::string(name).c_str()))
        return value;
    return fallback;
}

struct configuration
{
    std::string host, username, password, ca_file;
    std::uint16_t port{};
    std::string exchange, queue, routing_key;
    std::size_t publisher_concurrency{}, consumer_concurrency{}, message_count{};
    std::uint16_t prefetch{};

    static auto from_environment() -> configuration
    {
        configuration value;
        value.host = environment("CNETMOD_AMQP091_HOST", "127.0.0.1");
        value.port = static_cast<std::uint16_t>(
            std::stoi(environment("CNETMOD_AMQP091_PORT", "5672")));
        value.username = environment("CNETMOD_AMQP091_USERNAME", "guest");
        value.password = environment("CNETMOD_AMQP091_PASSWORD", "guest");
        value.ca_file = environment("CNETMOD_AMQP091_CA_FILE");
        value.exchange = environment("CNETMOD_AMQP091_EXCHANGE", "orders.events");
        value.queue = environment("CNETMOD_AMQP091_QUEUE", "orders.created.worker");
        value.routing_key = environment("CNETMOD_AMQP091_ROUTING_KEY", "orders.created");
        value.publisher_concurrency = std::max<std::size_t>(1,
            std::stoull(environment("CNETMOD_AMQP091_PUBLISHER_CONCURRENCY", "8")));
        value.consumer_concurrency = std::max<std::size_t>(1,
            std::stoull(environment("CNETMOD_AMQP091_CONSUMER_CONCURRENCY", "4")));
        value.message_count = std::max<std::size_t>(1,
            std::stoull(environment("CNETMOD_AMQP091_MESSAGE_COUNT", "10000")));
        value.prefetch = static_cast<std::uint16_t>(
            std::stoi(environment("CNETMOD_AMQP091_PREFETCH", "128")));
        return value;
    }

    auto connection_options() const -> cnetmod::amqp091::connection_options
    {
        cnetmod::amqp091::connection_options options;
        options.endpoint.host = host;
        options.endpoint.port = port;
        options.credentials.username = username;
        options.credentials.password = password;
        options.connection_name = "orders-service";
        options.heartbeat = std::chrono::seconds{15};
        options.automatic_recovery = true;
        if (!ca_file.empty()) {
            options.endpoint.tls.enabled = true;
            options.endpoint.tls.ca_file = ca_file;
            options.endpoint.tls.server_name = host;
        }
        return options;
    }
};

inline auto body(std::string_view text) -> std::vector<std::byte>
{
    auto raw = std::as_bytes(std::span{text});
    return {raw.begin(), raw.end()};
}

} // namespace amqp091_example
