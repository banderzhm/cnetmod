module cnetmod.application.rest_template;

import cnetmod.observability.http;

namespace cnetmod::application {

rest_template::rest_template(io_context& io,
    observability::telemetry_hub& telemetry, rest_template_options options)
    : telemetry_(telemetry), default_headers_(std::move(options.default_headers))
{
    client_shards_.push_back(client_pool_shard{
        .io = &io,
        .clients = std::make_unique<http::client_pool>(
            io, std::move(options.client), options.max_idle),
    });
}

rest_template::rest_template(std::span<io_context* const> event_loops,
    observability::telemetry_hub& telemetry, rest_template_options options)
    : telemetry_(telemetry), default_headers_(std::move(options.default_headers))
{
    if (event_loops.empty())
        throw std::invalid_argument{"rest_template requires an event loop"};
    client_shards_.reserve(event_loops.size());
    for (auto* io : event_loops)
    {
        if (io == nullptr)
            throw std::invalid_argument{"rest_template event loop is null"};
        client_shards_.push_back(client_pool_shard{
            .io = io,
            .clients = std::make_unique<http::client_pool>(
                *io, options.client, options.max_idle),
        });
    }
}

rest_template::rest_template(io_context& io,
    observability::telemetry_hub& telemetry, http::client_options options,
    std::size_t max_idle)
    : rest_template(io, telemetry,
          rest_template_options{
              .client = std::move(options),
              .max_idle = max_idle})
{
}

auto rest_template::exchange(const http::request& request,
    const http::tracing::trace_context& parent)
    -> task<std::expected<http::response, std::error_code>>
{
    co_return co_await exchange_impl(request, parent, nullptr);
}

auto rest_template::exchange(const http::request& request,
    const http::tracing::trace_context& parent, cancel_token& cancellation)
    -> task<std::expected<http::response, std::error_code>>
{
    co_return co_await exchange_impl(request, parent, &cancellation);
}

auto rest_template::get(std::string_view url,
    const rest_request_options& options)
    -> task<std::expected<http::response, std::error_code>>
{
    auto request = make_request(http::http_method::GET, url, {}, options);
    co_return co_await exchange(request, options.parent);
}

auto rest_template::get(std::string_view url,
    const rest_request_options& options, cancel_token& cancellation)
    -> task<std::expected<http::response, std::error_code>>
{
    auto request = make_request(http::http_method::GET, url, {}, options);
    co_return co_await exchange(request, options.parent, cancellation);
}

auto rest_template::post(std::string_view url, std::string body,
    const rest_request_options& options)
    -> task<std::expected<http::response, std::error_code>>
{
    auto request = make_request(
        http::http_method::POST, url, std::move(body), options);
    co_return co_await exchange(request, options.parent);
}

auto rest_template::post(std::string_view url, std::string body,
    const rest_request_options& options, cancel_token& cancellation)
    -> task<std::expected<http::response, std::error_code>>
{
    auto request = make_request(
        http::http_method::POST, url, std::move(body), options);
    co_return co_await exchange(request, options.parent, cancellation);
}

auto rest_template::put(std::string_view url, std::string body,
    const rest_request_options& options)
    -> task<std::expected<http::response, std::error_code>>
{
    auto request = make_request(
        http::http_method::PUT, url, std::move(body), options);
    co_return co_await exchange(request, options.parent);
}

auto rest_template::put(std::string_view url, std::string body,
    const rest_request_options& options, cancel_token& cancellation)
    -> task<std::expected<http::response, std::error_code>>
{
    auto request = make_request(
        http::http_method::PUT, url, std::move(body), options);
    co_return co_await exchange(request, options.parent, cancellation);
}

auto rest_template::patch(std::string_view url, std::string body,
    const rest_request_options& options)
    -> task<std::expected<http::response, std::error_code>>
{
    auto request = make_request(
        http::http_method::PATCH, url, std::move(body), options);
    co_return co_await exchange(request, options.parent);
}

auto rest_template::patch(std::string_view url, std::string body,
    const rest_request_options& options, cancel_token& cancellation)
    -> task<std::expected<http::response, std::error_code>>
{
    auto request = make_request(
        http::http_method::PATCH, url, std::move(body), options);
    co_return co_await exchange(request, options.parent, cancellation);
}

auto rest_template::remove(std::string_view url,
    const rest_request_options& options)
    -> task<std::expected<http::response, std::error_code>>
{
    auto request = make_request(http::http_method::DELETE_, url, {}, options);
    co_return co_await exchange(request, options.parent);
}

auto rest_template::remove(std::string_view url,
    const rest_request_options& options, cancel_token& cancellation)
    -> task<std::expected<http::response, std::error_code>>
{
    auto request = make_request(http::http_method::DELETE_, url, {}, options);
    co_return co_await exchange(request, options.parent, cancellation);
}

void rest_template::clear() noexcept
{
    for (auto& shard : client_shards_)
        shard.clients->clear();
}

auto rest_template::idle_count() const noexcept -> std::size_t
{
    std::size_t total = 0;
    for (const auto& shard : client_shards_)
        total += shard.clients->idle_count();
    return total;
}

auto rest_template::clients() noexcept -> http::client_pool&
{
    auto* current = io_context::current();
    if (current)
        for (auto& shard : client_shards_)
            if (shard.io == current)
                return *shard.clients;
    return *client_shards_.front().clients;
}

auto rest_template::exchange_impl(const http::request& request,
    const http::tracing::trace_context& parent, cancel_token* cancellation)
    -> task<std::expected<http::response, std::error_code>>
{
    auto& pool = clients();
    auto client = pool.acquire();
    auto outgoing = request;
    for (const auto& [name, value] : default_headers_)
    {
        if (outgoing.headers().find(name) == outgoing.headers().end())
            outgoing.set_header(name, value);
    }
    observability::instrumented_http_client observed{
        *client, telemetry_.spans(), telemetry_.measurements()};
    auto response = cancellation
        ? co_await observed.send(outgoing, parent, *cancellation)
        : co_await observed.send(outgoing, parent);
    if (response)
        pool.release(std::move(client));
    else
        client->close();
    co_return response;
}

auto rest_template::make_request(http::http_method method,
    std::string_view url, std::string body,
    const rest_request_options& options) const -> http::request
{
    http::request request{method, url};
    for (const auto& [name, value] : options.headers)
        request.set_header(name, value);
    if (method == http::http_method::POST || method == http::http_method::PUT ||
        method == http::http_method::PATCH)
        request.set_body(std::move(body));
    return request;
}

} // namespace cnetmod::application
