export module cnetmod.protocol.mongodb:connection_options;

import std;
import :authentication;
import :aws_credentials;
import :oidc_credentials;

export namespace cnetmod::mongodb {

struct connection_options
{
    std::string host = "127.0.0.1";
    std::uint16_t port = 27017;
    std::string database = "admin";
    std::string username;
    std::string password;
    std::string authentication_database = "admin";
    authentication_mechanism authentication = authentication_mechanism::automatic;
    bool authentication_database_explicit = false;
    std::shared_ptr<aws_credential_cache> aws_provider;
    std::shared_ptr<oidc_token_cache> oidc_provider;
    oidc_environment oidc_application_environment = oidc_environment::none;
    std::string oidc_token_resource;
    bool load_balanced = false;
    bool tls = false;
    bool tls_verify = true;
    std::string tls_ca_file;
    std::string tls_cert_file;
    std::string tls_key_file;
    std::string tls_sni;
    std::chrono::milliseconds connect_timeout{10000};
    std::chrono::milliseconds command_timeout{30000};
    bool enable_zlib_compression = true;
    bool enable_zstd_compression = false;
    bool enable_snappy_compression = false;
    std::vector<std::uint8_t> compression_preference;
    std::size_t compression_minimum_bytes = 1024;
    std::size_t max_message_bytes = 48 * 1024 * 1024;
    std::size_t max_bson_document_bytes = 16 * 1024 * 1024;
};

struct server_capabilities
{
    std::int32_t minimum_wire_version = 0;
    std::int32_t maximum_wire_version = 0;
    std::int32_t maximum_bson_object_size = 16 * 1024 * 1024;
    std::int32_t maximum_message_size_bytes = 48 * 1024 * 1024;
    std::int32_t maximum_write_batch_size = 100000;
    std::string server_type;
    bool writable_primary = false;
    bool sessions_supported = false;
    std::optional<std::uint8_t> selected_compressor;
};

} // namespace cnetmod::mongodb
