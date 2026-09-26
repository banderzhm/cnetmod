/**
 * @brief Application-managed outbound HTTP operations.
 */
export module cnetmod.application.rest_template;

import std;
import cnetmod.coro.cancel;
import cnetmod.coro.task;
import cnetmod.io.io_context;
import cnetmod.observability;
import cnetmod.protocol.http;
import cnetmod.protocol.http.client.pool;
import cnetmod.protocol.http.middleware.tracing;

namespace cnetmod::application {

/**
 * @brief Construction policy for an application REST template.
 */
export struct rest_template_options
{
    /**
     * @brief Configures protocol negotiation, TLS, redirects, and timeouts.
     */
    http::client_options client;

    /**
     * @brief Headers added when a request does not provide the same name.
     */
    http::header_map default_headers;

    /**
     * @brief Maximum number of successfully used clients retained for reuse.
     */
    std::size_t max_idle = 64;
};

/**
 * @brief Per-request headers and parent trace context.
 */
export struct rest_request_options
{
    /**
     * @brief Headers applied to this request and preferred over defaults.
     */
    http::header_map headers;

    /**
     * @brief Parent context used for trace propagation and client spans.
     */
    http::tracing::trace_context parent;
};

/**
 * @brief Provides pooled and observable outbound HTTP requests.
 *
 * The template owns its client pool but not the application event loop or
 * telemetry hub. Application code normally obtains it from
 * application_runtime::rest(). A failed or cancelled request is never returned
 * to the reusable client pool.
 */
export class rest_template
{
public:
    /**
     * @brief Binds outbound requests to application-owned infrastructure.
     */
    rest_template(io_context& io, observability::telemetry_hub& telemetry,
        rest_template_options options = {});
    rest_template(std::span<io_context* const> event_loops,
        observability::telemetry_hub& telemetry,
        rest_template_options options = {});

    /**
     * @brief Binds a template with protocol-client options only.
     */
    rest_template(io_context& io, observability::telemetry_hub& telemetry,
        http::client_options options, std::size_t max_idle = 64);

    rest_template(const rest_template&) = delete;
    auto operator=(const rest_template&) -> rest_template& = delete;

    /**
     * @brief Executes a fully configured HTTP request.
     */
    [[nodiscard]] auto exchange(const http::request& request,
        const http::tracing::trace_context& parent = {})
        -> task<std::expected<http::response, std::error_code>>;

    /**
     * @brief Executes a request with operation-scoped cancellation.
     */
    [[nodiscard]] auto exchange(const http::request& request,
        const http::tracing::trace_context& parent,
        cancel_token& cancellation)
        -> task<std::expected<http::response, std::error_code>>;

    /**
     * @brief Executes an HTTP GET request.
     */
    [[nodiscard]] auto get(std::string_view url,
        const rest_request_options& options = {})
        -> task<std::expected<http::response, std::error_code>>;

    /**
     * @brief Executes a cancellable HTTP GET request.
     */
    [[nodiscard]] auto get(std::string_view url,
        const rest_request_options& options, cancel_token& cancellation)
        -> task<std::expected<http::response, std::error_code>>;

    /**
     * @brief Executes an HTTP POST request.
     */
    [[nodiscard]] auto post(std::string_view url, std::string body,
        const rest_request_options& options = {})
        -> task<std::expected<http::response, std::error_code>>;

    /**
     * @brief Executes a cancellable HTTP POST request.
     */
    [[nodiscard]] auto post(std::string_view url, std::string body,
        const rest_request_options& options, cancel_token& cancellation)
        -> task<std::expected<http::response, std::error_code>>;

    /**
     * @brief Executes an HTTP PUT request.
     */
    [[nodiscard]] auto put(std::string_view url, std::string body,
        const rest_request_options& options = {})
        -> task<std::expected<http::response, std::error_code>>;

    /**
     * @brief Executes a cancellable HTTP PUT request.
     */
    [[nodiscard]] auto put(std::string_view url, std::string body,
        const rest_request_options& options, cancel_token& cancellation)
        -> task<std::expected<http::response, std::error_code>>;

    /**
     * @brief Executes an HTTP PATCH request.
     */
    [[nodiscard]] auto patch(std::string_view url, std::string body,
        const rest_request_options& options = {})
        -> task<std::expected<http::response, std::error_code>>;

    /**
     * @brief Executes a cancellable HTTP PATCH request.
     */
    [[nodiscard]] auto patch(std::string_view url, std::string body,
        const rest_request_options& options, cancel_token& cancellation)
        -> task<std::expected<http::response, std::error_code>>;

    /**
     * @brief Executes an HTTP DELETE request.
     */
    [[nodiscard]] auto remove(std::string_view url,
        const rest_request_options& options = {})
        -> task<std::expected<http::response, std::error_code>>;

    /**
     * @brief Executes a cancellable HTTP DELETE request.
     */
    [[nodiscard]] auto remove(std::string_view url,
        const rest_request_options& options, cancel_token& cancellation)
        -> task<std::expected<http::response, std::error_code>>;

    /**
     * @brief Closes every idle pooled connection.
     */
    void clear() noexcept;

    /**
     * @brief Returns the current number of idle pooled clients.
     */
    [[nodiscard]] auto idle_count() const noexcept -> std::size_t;

private:
    auto exchange_impl(const http::request& request,
        const http::tracing::trace_context& parent,
        cancel_token* cancellation)
        -> task<std::expected<http::response, std::error_code>>;

    [[nodiscard]] auto make_request(http::http_method method,
        std::string_view url, std::string body,
        const rest_request_options& options) const -> http::request;

    struct client_pool_shard
    {
        io_context* io{};
        std::unique_ptr<http::client_pool> clients;
    };

    [[nodiscard]] auto clients() noexcept -> http::client_pool&;

    std::vector<client_pool_shard> client_shards_;
    observability::telemetry_hub& telemetry_;
    http::header_map default_headers_;
};

} // namespace cnetmod::application
