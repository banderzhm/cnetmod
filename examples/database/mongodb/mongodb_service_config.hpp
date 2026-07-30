#pragma once

namespace mongodb_example {

inline auto environment(std::string_view name, std::string fallback = {}) -> std::string
{
    if (const char* value = std::getenv(std::string(name).c_str()))
        return value;
    return fallback;
}

inline auto environment_flag(std::string_view name, bool fallback) -> bool
{
    const auto value = environment(name, fallback ? "true" : "false");
    return value == "1" || value == "true" || value == "TRUE" || value == "yes";
}

inline auto environment_size(std::string_view name, std::size_t fallback,
    std::size_t minimum, std::size_t maximum) -> std::size_t
{
    return std::clamp<std::size_t>(std::stoull(environment(name,
                                       std::to_string(fallback))),
        minimum, maximum);
}

struct service_config
{
    std::vector<cnetmod::mongodb::server_address> seeds;
    std::optional<std::string> replica_set_name;
    std::string username;
    std::string password;
    std::string database;
    std::string authentication_database;
    bool tls = false;
    std::string ca_file;
    std::size_t pool_minimum_size = 4;
    std::size_t pool_maximum_size = 64;
    std::size_t pool_maximum_connecting = 4;
    std::chrono::milliseconds pool_wait_timeout{5000};
    std::chrono::milliseconds command_timeout{15000};
    std::chrono::milliseconds heartbeat_interval{5000};
    std::chrono::milliseconds health_interval{5000};
    std::chrono::milliseconds change_stream_maximum_await{1000};
    std::chrono::milliseconds shutdown_timeout{15000};
    std::chrono::milliseconds scenario_duration{20000};
    std::size_t worker_concurrency = 32;
    std::size_t request_count = 10000;
    std::size_t queue_capacity = 2048;
    std::string test_run_id;

    static auto from_environment() -> service_config
    {
        service_config value;
        auto seed_text = environment("CNETMOD_MONGODB_SEEDS", "127.0.0.1:27017");
        for (auto part : seed_text | std::views::split(','))
        {
            std::string seed(part.begin(), part.end());
            auto parsed = cnetmod::mongodb::parse_server_address(seed);
            if (!parsed)
                throw std::invalid_argument("invalid CNETMOD_MONGODB_SEEDS: " + parsed.error().message);
            value.seeds.push_back(*parsed);
        }
        auto replica_set = environment("CNETMOD_MONGODB_REPLICA_SET");
        if (!replica_set.empty())
            value.replica_set_name = std::move(replica_set);
        value.username = environment("CNETMOD_MONGODB_USERNAME", "cnetmod");
        value.password = environment("CNETMOD_MONGODB_PASSWORD");
        value.database = environment("CNETMOD_MONGODB_DATABASE", "cnetmod");
        value.authentication_database = environment("CNETMOD_MONGODB_AUTH_DATABASE", "admin");
        value.tls = environment_flag("CNETMOD_MONGODB_TLS", false);
        value.ca_file = environment("CNETMOD_MONGODB_CA_FILE");
        value.pool_minimum_size = environment_size("CNETMOD_MONGODB_POOL_MIN", 4, 0, 256);
        value.pool_maximum_size = environment_size("CNETMOD_MONGODB_POOL_MAX", 64, 1, 1024);
        value.pool_minimum_size = std::min(value.pool_minimum_size, value.pool_maximum_size);
        value.pool_maximum_connecting = environment_size("CNETMOD_MONGODB_POOL_MAX_CONNECTING", 4, 1, 64);
        value.pool_wait_timeout = std::chrono::milliseconds(environment_size("CNETMOD_MONGODB_WAIT_TIMEOUT_MS", 5000, 1, 300000));
        value.command_timeout = std::chrono::milliseconds(environment_size("CNETMOD_MONGODB_COMMAND_TIMEOUT_MS", 15000, 100, 300000));
        value.heartbeat_interval = std::chrono::milliseconds(environment_size("CNETMOD_MONGODB_HEARTBEAT_MS", 5000, 500, 300000));
        value.health_interval = std::chrono::milliseconds(environment_size("CNETMOD_MONGODB_HEALTH_MS", 5000, 500, 300000));
        value.change_stream_maximum_await = std::chrono::milliseconds(environment_size("CNETMOD_MONGODB_CHANGE_AWAIT_MS", 1000, 100, 30000));
        value.shutdown_timeout = std::chrono::milliseconds(environment_size("CNETMOD_MONGODB_SHUTDOWN_MS", 15000, 1000, 300000));
        value.scenario_duration = std::chrono::milliseconds(environment_size("CNETMOD_MONGODB_SCENARIO_MS", 20000, 1000, 300000));
        value.worker_concurrency = environment_size("CNETMOD_MONGODB_WORKERS", 32, 1, 512);
        value.request_count = environment_size("CNETMOD_MONGODB_REQUESTS", 10000, 1, 10000000);
        value.queue_capacity = environment_size("CNETMOD_MONGODB_QUEUE_CAPACITY", 2048, 1, 1000000);
        value.test_run_id = environment("CNETMOD_MONGODB_TEST_RUN_ID",
            std::format("run-{}", std::chrono::system_clock::now().time_since_epoch().count()));
        return value;
    }

    auto connection_options_for(const cnetmod::mongodb::server_address& address) const
        -> cnetmod::mongodb::connection_options
    {
        cnetmod::mongodb::connection_options options;
        options.host = address.host;
        options.port = address.port;
        options.username = username;
        options.password = password;
        options.database = database;
        options.authentication_database = authentication_database;
        options.tls = tls;
        options.tls_verify = true;
        options.tls_ca_file = ca_file;
        options.tls_sni = address.host;
        options.connect_timeout = std::chrono::seconds{10};
        options.command_timeout = command_timeout;
        return options;
    }

    auto topology_pool_options() const -> cnetmod::mongodb::topology_connection_pool_options
    {
        cnetmod::mongodb::topology_connection_pool_options options;
        options.seeds = seeds;
        options.replica_set_name = replica_set_name;
        options.per_server_pool.connection = connection_options_for(seeds.front());
        options.per_server_pool.minimum_size = pool_minimum_size;
        options.per_server_pool.maximum_size = pool_maximum_size;
        options.per_server_pool.maximum_connecting = pool_maximum_connecting;
        options.per_server_pool.wait_queue_timeout = pool_wait_timeout;
        options.per_server_pool.health_check_interval = health_interval;
        return options;
    }
};

} // namespace mongodb_example
