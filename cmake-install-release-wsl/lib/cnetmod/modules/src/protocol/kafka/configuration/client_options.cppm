module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.kafka.client_options;

import std;
import cnetmod.protocol.kafka.protocol_constants;

export namespace cnetmod::kafka {

struct tls_options
{
    bool enabled = false;
    bool verify_peer = true;
    std::string ca_file;
    std::string certificate_file;
    std::string private_key_file;
    std::string server_name;
};

struct client_endpoint
{
    std::string host = "127.0.0.1";
    std::uint16_t port = 9092;
    std::chrono::milliseconds connect_timeout = std::chrono::seconds(10);
    tls_options tls;
};

struct authentication_credentials
{
    std::string username;
    std::string password;
};

struct client_options
{
    std::vector<client_endpoint> bootstrap_servers;
    std::string client_id = "cnetmod";
    authentication_credentials credentials;
    sasl_mechanism sasl = sasl_mechanism::none;
    std::chrono::milliseconds request_timeout{30000};
    std::chrono::milliseconds retry_backoff{100};
    std::chrono::milliseconds retry_backoff_max{1000};
    std::chrono::milliseconds metadata_refresh_interval{300000};
    std::size_t retries = 5;
    std::size_t max_response_bytes = 100 * 1024 * 1024;
    std::shared_ptr<scram_crypto_provider> scram_crypto;
};

} // namespace cnetmod::kafka
