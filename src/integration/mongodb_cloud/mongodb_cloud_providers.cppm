export module cnetmod.integration.mongodb_cloud;
import std;
import cnetmod.coro.task;
import cnetmod.io.io_context;
import cnetmod.executor.pool;
import cnetmod.protocol.mongodb;

export namespace cnetmod::mongodb::cloud {
enum class metadata_method
{
    get,
    post,
    put
};

struct metadata_request
{
    metadata_method method = metadata_method::get;
    std::string url;
    std::map<std::string, std::string> headers;
    std::string body;
    std::chrono::milliseconds timeout{10000};
};

struct metadata_response
{
    int status = 0;
    std::string body;
};

using metadata_transport = std::function<task<result<metadata_response>>(metadata_request)>;

struct oidc_workload_options
{
    oidc_environment environment = oidc_environment::none;
    std::string token_resource;
    std::string username;
    std::chrono::milliseconds timeout{60000};
};

/// HTTP/1 transport: TLS verified, redirects/cookies off, response capped at 1 MiB.
auto make_metadata_transport(io_context& io) -> metadata_transport;
auto make_oidc_workload_provider(io_context& io, thread_pool& pool, oidc_workload_options options,
    metadata_transport transport = {}) -> result<std::shared_ptr<oidc_token_cache>>;
auto make_default_aws_provider(io_context& io, thread_pool& pool,
    metadata_transport transport = {}) -> std::shared_ptr<aws_credential_cache>;
/// Wire provider selection at the composition boundary, never in core transport.
auto configure_cloud_credentials(connection_options& options, io_context& io, thread_pool& pool,
    metadata_transport transport = {}) -> result<void>;
} // namespace cnetmod::mongodb::cloud

namespace cnetmod::mongodb::cloud::detail {
auto environment_value(std::string_view key) -> std::string;
auto safe_role_name(std::string_view name) -> bool;
auto ecs_endpoint() -> result<std::optional<std::string>>;
auto fetch_web_identity(io_context& io, thread_pool& pool, metadata_transport& transport,
    std::string token_file, std::string role) -> task<result<aws_credentials>>;
auto fetch_ecs_credentials(io_context& io, thread_pool& pool, metadata_transport& transport,
    std::string endpoint) -> task<result<aws_credentials>>;
auto fetch_instance_credentials(metadata_transport& transport) -> task<result<aws_credentials>>;
auto percent_encode(std::string_view value) -> std::string;
auto read_token_file(const std::filesystem::path& path) -> result<std::string>;
auto parse_utc_expiry(std::string_view value) -> result<std::chrono::system_clock::time_point>;
auto parse_aws_json_credentials(std::string_view value) -> result<aws_credentials>;
auto parse_aws_sts_credentials(std::string_view value) -> result<aws_credentials>;
} // namespace cnetmod::mongodb::cloud::detail
