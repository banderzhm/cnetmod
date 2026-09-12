module cnetmod.observability.http;

import std;

namespace cnetmod::observability {
namespace {

    auto begin_http_span(const http::request& request,
        const http::tracing::trace_context& parent)
        -> http::tracing::active_span
    {
        return http::tracing::start_client_span(parent,
            std::format("{}", http::method_to_string(request.method())),
            {{"http.request.method", std::string{http::method_to_string(request.method())}},
                {"url.full", std::string{request.uri()}}});
    }

} // namespace

instrumented_http_client::instrumented_http_client(http::client& client,
    http::tracing::span_exporter exporter) noexcept
    : client_(&client), exporter_(std::move(exporter))
{
}

auto instrumented_http_client::send(const http::request& request,
    const http::tracing::trace_context& parent)
    -> task<std::expected<http::response, std::error_code>>
{
    auto span = begin_http_span(request, parent);
    auto propagated = request;
    http::tracing::inject(propagated, span.context);
    auto result = co_await client_->send(propagated);
    if (result)
    {
        span.attributes.emplace_back("http.response.status_code",
            std::to_string(result->status_code()));
    }
    report(std::move(span), !result || result->status_code() >= 400);
    co_return result;
}

auto instrumented_http_client::send(const http::request& request,
    const http::tracing::trace_context& parent, cancel_token& cancellation)
    -> task<std::expected<http::response, std::error_code>>
{
    auto span = begin_http_span(request, parent);
    auto propagated = request;
    http::tracing::inject(propagated, span.context);
    auto result = co_await client_->send(propagated, cancellation);
    if (result)
    {
        span.attributes.emplace_back("http.response.status_code",
            std::to_string(result->status_code()));
    }
    report(std::move(span), !result || result->status_code() >= 400);
    co_return result;
}

void instrumented_http_client::report(http::tracing::active_span span,
    bool failed) const noexcept
{
    if (!exporter_)
        return;
    try
    {
        exporter_(http::tracing::finish_client_span(std::move(span), failed));
    }
    catch (...)
    {
        // Instrumentation must never alter the observed request.
    }
}

} // namespace cnetmod::observability
