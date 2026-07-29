#pragma once

namespace kafka_example {

inline auto environment(std::string_view name, std::string fallback = {}) -> std::string
{
    if (const char* value = std::getenv(std::string(name).c_str()))
        return value;
    return fallback;
}

struct configuration
{
    std::string host;
    std::uint16_t port{};
    std::string topic;
    std::string group_id;
    std::string username;
    std::string password;
    std::string ca_file;
    std::size_t producer_concurrency{};
    std::size_t consumer_concurrency{};
    std::size_t message_count{};

    static auto from_environment() -> configuration
    {
        configuration value;
        value.host = environment("CNETMOD_KAFKA_HOST", "127.0.0.1");
        value.port = static_cast<std::uint16_t>(
            std::stoi(environment("CNETMOD_KAFKA_PORT", "9092")));
        value.topic = environment("CNETMOD_KAFKA_TOPIC", "cnetmod-example");
        value.group_id = environment("CNETMOD_KAFKA_GROUP", "cnetmod-example-group");
        value.username = environment("CNETMOD_KAFKA_USERNAME");
        value.password = environment("CNETMOD_KAFKA_PASSWORD");
        value.ca_file = environment("CNETMOD_KAFKA_CA_FILE");
        value.producer_concurrency = std::max<std::size_t>(1,
            std::stoull(environment("CNETMOD_KAFKA_PRODUCER_CONCURRENCY", "8")));
        value.consumer_concurrency = std::max<std::size_t>(1,
            std::stoull(environment("CNETMOD_KAFKA_CONSUMER_CONCURRENCY", "4")));
        value.message_count = std::max<std::size_t>(1,
            std::stoull(environment("CNETMOD_KAFKA_MESSAGE_COUNT", "10000")));
        return value;
    }

    auto client_options(std::string client_id) const -> cnetmod::kafka::client_options
    {
        namespace kafka = cnetmod::kafka;
        kafka::client_endpoint endpoint;
        endpoint.host = host;
        endpoint.port = port;
        if (!ca_file.empty()) {
            endpoint.tls.enabled = true;
            endpoint.tls.ca_file = ca_file;
            endpoint.tls.server_name = host;
        }
        kafka::client_options options;
        options.bootstrap_servers.push_back(std::move(endpoint));
        options.client_id = std::move(client_id);
        options.credentials = {.username = username, .password = password};
        if (!username.empty())
            options.sasl = kafka::sasl_mechanism::plain;
        options.request_timeout = std::chrono::seconds{30};
        options.retries = 8;
        options.retry_backoff = std::chrono::milliseconds{200};
        options.retry_backoff_max = std::chrono::seconds{5};
        return options;
    }
};

inline auto bytes(std::string_view text) -> cnetmod::kafka::bytes
{
    auto raw = std::as_bytes(std::span{text});
    return {raw.begin(), raw.end()};
}

} // namespace kafka_example
