export module cnetmod.protocol.postgresql:connection_options;

import std;

export namespace cnetmod::postgresql {

enum class tls_mode : std::uint8_t
{
    disable,
    prefer,
    require,
    verify_ca,
    verify_full
};

struct connection_options
{
    std::string host = "localhost";
    std::uint16_t port = 5432;
    std::string username = "postgres";
    std::string password;
    std::string database = "postgres";
    std::string application_name = "cnetmod";
    tls_mode tls = tls_mode::prefer;
    std::string tls_ca_file;
    std::string tls_cert_file;
    std::string tls_key_file;
    std::chrono::milliseconds connect_timeout{10000};
    std::size_t maximum_connect_attempts = 3;
    std::chrono::milliseconds connect_retry_backoff{100};
    std::size_t maximum_message_size = 64U * 1024U * 1024U;
    std::size_t maximum_column_count = 65535;
    std::size_t maximum_row_count = 1000000;
    std::unordered_map<std::string, std::string> startup_parameters;
};

} // namespace cnetmod::postgresql
