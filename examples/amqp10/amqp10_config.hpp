#pragma once

namespace amqp10_example {

inline auto environment(std::string_view name, std::string fallback = {}) -> std::string
{
    if (const char* value = std::getenv(std::string(name).c_str()))
        return value;
    return fallback;
}

struct configuration
{
    std::string host, username, password, ca_file, address;
    std::uint16_t port{};
    std::size_t sender_concurrency{}, receiver_concurrency{}, message_count{};
    std::uint32_t receiver_credit{};

    static auto from_environment() -> configuration
    {
        configuration value;
        value.host = environment("CNETMOD_AMQP10_HOST", "127.0.0.1");
        value.port = static_cast<std::uint16_t>(
            std::stoi(environment("CNETMOD_AMQP10_PORT", "5672")));
        value.username = environment("CNETMOD_AMQP10_USERNAME", "artemis");
        value.password = environment("CNETMOD_AMQP10_PASSWORD", "artemis");
        value.ca_file = environment("CNETMOD_AMQP10_CA_FILE");
        value.address = environment("CNETMOD_AMQP10_ADDRESS", "orders.created");
        value.sender_concurrency = std::max<std::size_t>(1,
            std::stoull(environment("CNETMOD_AMQP10_SENDER_CONCURRENCY", "8")));
        value.receiver_concurrency = std::max<std::size_t>(1,
            std::stoull(environment("CNETMOD_AMQP10_RECEIVER_CONCURRENCY", "4")));
        value.message_count = std::max<std::size_t>(1,
            std::stoull(environment("CNETMOD_AMQP10_MESSAGE_COUNT", "10000")));
        value.receiver_credit = static_cast<std::uint32_t>(
            std::stoul(environment("CNETMOD_AMQP10_RECEIVER_CREDIT", "128")));
        return value;
    }

    auto client_options() const -> cnetmod::amqp10::client_options
    {
        cnetmod::amqp10::client_options options;
        options.endpoint.host = host;
        options.endpoint.port = port;
        options.credentials.username = username;
        options.credentials.password = password;
        options.container_id = "orders-service";
        options.idle_timeout = std::chrono::seconds{30};
        options.reconnect = std::make_shared<cnetmod::amqp10::exponential_backoff>(
            std::chrono::seconds{1}, std::chrono::seconds{30}, 2.0);
        options.recover_sessions = true;
        if (!ca_file.empty()) {
            options.endpoint.tls.enabled = true;
            options.endpoint.tls.ca_file = ca_file;
            options.endpoint.tls.server_name = host;
        }
        return options;
    }
};

} // namespace amqp10_example
