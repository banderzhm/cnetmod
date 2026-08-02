module;
#include <cnetmod/config.hpp>
export module cnetmod.protocol.amqp091:connection_options;
import std;

export namespace cnetmod::amqp091 {

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
};

struct credentials
{
    authentication_mechanism mechanism = authentication_mechanism::plain;
    std::string username;
    std::string password;
};

struct endpoint
{
    std::string host = "127.0.0.1";
    std::uint16_t port = 5672;
    std::chrono::milliseconds connect_timeout = std::chrono::seconds(10);
    tls_options tls;
};

struct connection_options
{
    amqp091::endpoint endpoint{};
    amqp091::credentials credentials{};
    std::string virtual_host = "/";
    std::string locale = "en_US";
    std::string connection_name;
    std::uint16_t channel_max = 0;
    std::uint32_t frame_max = 131072;
    std::chrono::seconds heartbeat{60};
    bool automatic_recovery = true;
};
} // namespace cnetmod::amqp091
