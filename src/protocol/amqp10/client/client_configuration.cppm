module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.amqp10:client_configuration;

import std;

export namespace cnetmod::amqp10 {

struct tls_options
{
    bool enabled = false;
    bool verify_peer = true;
    std::string ca_file;
    std::string certificate_file;
    std::string private_key_file;
    std::string server_name;
};

enum class authentication_mechanism
{
    anonymous,
    plain,
    external,
    scram_sha_256,
    scram_sha_512,
    oauth_bearer,
};

struct credentials
{
    authentication_mechanism mechanism = authentication_mechanism::plain;
    std::string username;
    std::string password;
    std::string token;
};

struct endpoint
{
    std::string host = "127.0.0.1";
    std::uint16_t port = 5672;
    std::chrono::milliseconds connect_timeout = std::chrono::seconds(10);
    tls_options tls;
};

} // namespace cnetmod::amqp10
