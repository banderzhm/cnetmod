module cnetmod.protocol.http.reverse_proxy;

import std;
import cnetmod.core.error;
import cnetmod.coro.cancel;
import cnetmod.coro.timer;

#if defined(CNETMOD_HAS_SSL) && defined(CNETMOD_ENABLE_QUIC)
import cnetmod.protocol.http.v3.session;
#endif

namespace cnetmod::http {
namespace {

    auto ascii_equal(std::string_view left, std::string_view right) noexcept -> bool
    {
        if (left.size() != right.size())
            return false;
        for (std::size_t index = 0; index < left.size(); ++index)
        {
            const auto lhs = static_cast<unsigned char>(left[index]);
            const auto rhs = static_cast<unsigned char>(right[index]);
            const auto normalized_lhs =
                lhs >= 'A' && lhs <= 'Z' ? lhs + ('a' - 'A') : lhs;
            const auto normalized_rhs =
                rhs >= 'A' && rhs <= 'Z' ? rhs + ('a' - 'A') : rhs;
            if (normalized_lhs != normalized_rhs)
                return false;
        }
        return true;
    }

    void lowercase_ascii(std::string& value) noexcept
    {
        for (auto& character : value)
        {
            if (character >= 'A' && character <= 'Z')
                character = static_cast<char>(character + ('a' - 'A'));
        }
    }

    auto is_standard_hop_header(std::string_view name) noexcept -> bool
    {
        return ascii_equal(name, "Connection") ||
            ascii_equal(name, "Keep-Alive") ||
            ascii_equal(name, "Proxy-Authenticate") ||
            ascii_equal(name, "Proxy-Authorization") ||
            ascii_equal(name, "Proxy-Connection") ||
            ascii_equal(name, "TE") || ascii_equal(name, "Trailer") ||
            ascii_equal(name, "Transfer-Encoding") ||
            ascii_equal(name, "Upgrade");
    }

    auto connection_names_header(std::string_view name,
        std::string_view value) noexcept -> bool
    {
        while (!value.empty())
        {
            const auto comma = value.find(',');
            auto token = value.substr(0, comma);
            while (!token.empty() && (token.front() == ' ' || token.front() == '\t'))
                token.remove_prefix(1);
            while (!token.empty() && (token.back() == ' ' || token.back() == '\t'))
                token.remove_suffix(1);
            if (ascii_equal(name, token))
                return true;
            if (comma == std::string_view::npos)
                break;
            value.remove_prefix(comma + 1);
        }
        return false;
    }

    auto is_hop_header(std::string_view name,
        std::string_view connection_fields) noexcept -> bool
    {
        if (is_standard_hop_header(name))
            return true;
        return connection_names_header(name, connection_fields);
    }

    auto append_upstream_uri(std::string_view upstream, std::string_view uri)
        -> std::string
    {
        std::string result;
        result.reserve(upstream.size() + uri.size() + 1);
        result.append(upstream);
        if (uri.empty())
        {
            if (!result.ends_with('/'))
                result.push_back('/');
            return result;
        }
        if (result.ends_with('/') && uri.starts_with('/'))
            result.pop_back();
        else if (!result.ends_with('/') && !uri.starts_with('/'))
            result.push_back('/');
        result.append(uri);
        return result;
    }

    auto parse_content_length(std::string_view value)
        -> std::optional<std::uint64_t>
    {
        if (value.empty())
            return std::nullopt;
        std::uint64_t length{};
        const auto [end, error] =
            std::from_chars(value.data(), value.data() + value.size(), length);
        if (error != std::errc{} || end != value.data() + value.size())
            return std::nullopt;
        return length;
    }

    void add_forwarded_headers(request& output, request_context& context,
        const reverse_proxy_options& options)
    {
        if (options.forwarded_headers == forwarded_header_policy::omit)
            return;

        const auto peer = context.peer_address();
        const auto existing = context.get_header("X-Forwarded-For");
        if (options.forwarded_headers == forwarded_header_policy::append &&
            !existing.empty() && !peer.empty())
        {
            std::string value;
            value.reserve(existing.size() + peer.size() + 2);
            value.append(existing);
            value.append(", ");
            value.append(peer);
            output.set_header("X-Forwarded-For", value);
        }
        else if (!peer.empty())
        {
            output.set_header("X-Forwarded-For", peer);
        }

        if (!options.downstream_scheme.empty())
            output.set_header("X-Forwarded-Proto", options.downstream_scheme);
        const auto host = context.get_header("Host");
        if (!host.empty())
            output.set_header("X-Forwarded-Host", host);
    }

    void copy_request_headers(request_context& context, request& output,
        const reverse_proxy_options& options)
    {
        const auto connection_fields = context.get_header("Connection");
        const auto copy = [&](std::string_view name, std::string_view value)
        {
            if (is_hop_header(name, connection_fields) ||
                ascii_equal(name, "X-Forwarded-For") ||
                ascii_equal(name, "X-Forwarded-Host") ||
                ascii_equal(name, "X-Forwarded-Proto") ||
                (!options.preserve_host && ascii_equal(name, "Host")))
                return;
            output.set_header(name, value);
        };

        const auto borrowed = context.borrowed_header_views();
        if (!borrowed.empty())
        {
            for (const auto& header : borrowed)
                copy(header.name, header.value);
        }
        else
        {
            for (const auto& [name, value] : context.headers())
                copy(name, value);
        }
        add_forwarded_headers(output, context, options);
    }

    void transfer_response(response& upstream, response& downstream,
        http_method request_method)
    {
        const auto head_content_length = request_method == http_method::HEAD
            ? std::string{upstream.get_header("Content-Length")}
            : std::string{};
        downstream.set_status(upstream.status_code());
        upstream.take_headers(
            [&](std::string name, std::string value)
            {
                if (!is_standard_hop_header(name) &&
                    !ascii_equal(name, "Content-Length"))
                    downstream.set_header_owned(name, std::move(value));
            });

        if (request_method == http_method::HEAD)
        {
            if (!head_content_length.empty())
                downstream.set_header("Content-Length", head_content_length);
            downstream.set_body_preserve_headers(std::string{});
        }
        else if (auto shared = upstream.take_shared_body())
        {
            downstream.set_shared_body(std::move(shared));
        }
        else if (auto source = upstream.take_body_source())
        {
            downstream.set_body_source(std::move(source));
        }
        else
        {
            downstream.set_body(upstream.take_body());
        }

        for (auto& [name, value] : upstream.take_trailers())
            downstream.set_trailer(name, value);
    }

    auto is_timeout(std::error_code error) noexcept -> bool
    {
        return error == std::make_error_code(std::errc::timed_out);
    }

    auto child_deadline(std::chrono::milliseconds timeout) noexcept -> deadline
    {
        return timeout > std::chrono::milliseconds::zero()
            ? deadline::after(timeout)
            : deadline{};
    }

#if defined(CNETMOD_HAS_SSL) && defined(CNETMOD_ENABLE_QUIC)
    class cancellation_link final
    {
    public:
        cancellation_link(cancel_token& parent, cancel_token& child) noexcept
            : parent_(parent), child_(child)
        {
            if (!parent_.register_callback(this, [](void* value) noexcept
                    {
                        static_cast<cancellation_link*>(value)->child_.cancel();
                    }))
                child_.cancel();
        }

        ~cancellation_link()
        {
            (void)parent_.complete_callback(this);
            parent_.finish_callback(this);
        }

        cancellation_link(const cancellation_link&) = delete;
        auto operator=(const cancellation_link&) -> cancellation_link& = delete;

    private:
        cancel_token& parent_;
        cancel_token& child_;
    };

    void copy_http3_request(v3::http3_request& input, request& output,
        const reverse_proxy_options& options)
    {
        const auto connection = input.headers.find("Connection");
        const auto connection_fields = connection == input.headers.end()
            ? std::string_view{}
            : std::string_view{connection->second};
        for (auto& [name, value] : input.headers)
        {
            if (is_hop_header(name, connection_fields) ||
                ascii_equal(name, "X-Forwarded-For") ||
                ascii_equal(name, "X-Forwarded-Host") ||
                ascii_equal(name, "X-Forwarded-Proto") ||
                ascii_equal(name, "Host"))
                continue;
            output.set_header_owned(std::move(name), std::move(value));
        }
        input.headers.clear();

        if (options.preserve_host && !input.host.empty())
            output.set_header("Host", input.host);
        if (options.forwarded_headers != forwarded_header_policy::omit)
        {
            if (!input.host.empty())
                output.set_header("X-Forwarded-Host", input.host);
            const auto scheme = options.downstream_scheme.empty()
                ? std::string_view{input.scheme}
                : std::string_view{options.downstream_scheme};
            if (!scheme.empty())
                output.set_header("X-Forwarded-Proto", scheme);
        }
    }

    void transfer_http3_response(response& upstream,
        v3::http3_response& downstream, http_method request_method)
    {
        const auto head_content_length = request_method == http_method::HEAD
            ? std::string{upstream.get_header("Content-Length")}
            : std::string{};
        downstream.status = upstream.status_code();
        upstream.take_headers(
            [&](std::string name, std::string value)
            {
                if (!is_standard_hop_header(name) &&
                    !ascii_equal(name, "Content-Length"))
                {
                    lowercase_ascii(name);
                    downstream.headers[std::move(name)] = std::move(value);
                }
            });
        if (request_method == http_method::HEAD)
        {
            if (!head_content_length.empty())
                downstream.headers["content-length"] = head_content_length;
        }
        else if (auto shared = upstream.take_shared_body())
            downstream.shared_body = std::move(shared);
        else if (auto source = upstream.take_body_source())
            downstream.body_source = std::move(source);
        else
            downstream.body = upstream.take_body();
        auto trailers = upstream.take_trailers();
        for (auto& [name, value] : trailers)
        {
            lowercase_ascii(name);
            downstream.trailers[std::move(name)] = std::move(value);
        }
    }

    void set_http3_error(v3::http3_response& response, int status_code,
        std::string_view message)
    {
        response.status = status_code;
        response.headers["content-type"] = "text/plain";
        response.body.assign(message);
    }
#endif

} // namespace

auto reverse_proxy::create(io_context& context, reverse_proxy_options options)
    -> std::expected<std::unique_ptr<reverse_proxy>, std::error_code>
{
    const auto parsed = url::parse(options.upstream);
    if (!parsed || parsed->host.empty() ||
        (parsed->scheme != "http" && parsed->scheme != "https") ||
        !parsed->query.empty() || !parsed->fragment.empty())
        return std::unexpected(
            std::make_error_code(std::errc::invalid_argument));

    auto normalized = std::move(options.upstream);
    while (normalized.size() > parsed->scheme.size() + 3 &&
        normalized.ends_with('/'))
        normalized.pop_back();
    options.pool.client.follow_redirects = false;
    options.pool.client.enable_cookies = false;
    return std::unique_ptr<reverse_proxy>{new reverse_proxy{
        context, std::move(options), std::move(normalized)}};
}

reverse_proxy::reverse_proxy(io_context& context,
    reverse_proxy_options options, std::string normalized_upstream)
    : context_(context), options_(std::move(options)), upstream_(std::move(normalized_upstream)), pool_(context, options_.pool)
{
}

auto reverse_proxy::forward(request_context& context) -> task<void>
{
    const auto method = context.method_enum();
    if (!method)
    {
        context.text(status::bad_request, "unsupported request method");
        co_return;
    }

    request outgoing{*method, append_upstream_uri(upstream_, context.uri())};
    copy_request_headers(context, outgoing, options_);
    if (context.has_body_stream())
    {
        auto source = std::make_shared<request_body_source>(
            [&context](cancel_token& cancellation)
                -> task<std::optional<request_body_chunk>>
            {
                if (cancellation.is_cancelled())
                    co_return std::nullopt;
                co_return co_await context.receive_body_chunk();
            },
            parse_content_length(context.get_header("Content-Length")));
        outgoing.set_body_source(std::move(source));
    }
    else if (!context.body().empty())
    {
        outgoing.set_body(std::string{context.body()});
    }

    auto lease = co_await context.with_deadline(
        child_deadline(options_.pool.acquire_timeout),
        [&](cancel_token& cancellation)
        {
            return pool_.acquire(cancellation);
        });
    if (!lease)
    {
        if (context.cancellation_token().is_cancelled())
            co_return;
        context.text(is_timeout(lease.error()) ? status::gateway_timeout
                                               : status::service_unavailable,
            is_timeout(lease.error()) ? "upstream lease timed out"
                                      : "upstream capacity unavailable");
        co_return;
    }

    auto result = co_await context.with_deadline(
        child_deadline(options_.pool.client.request_timeout),
        [&](cancel_token& operation_cancellation)
        {
            return lease->get().send(outgoing, operation_cancellation);
        });
    if (!result)
    {
        lease->discard();
        if (context.cancellation_token().is_cancelled())
            co_return;
        context.text(is_timeout(result.error()) ? status::gateway_timeout
                                                : status::bad_gateway,
            is_timeout(result.error()) ? "upstream request timed out"
                                       : "upstream request failed");
        co_return;
    }
    transfer_response(*result, context.resp(), *method);
}

#if defined(CNETMOD_HAS_SSL) && defined(CNETMOD_ENABLE_QUIC)
auto reverse_proxy::forward(v3::http3_request& input,
    v3::http3_response& output, cancel_token& cancellation)
    -> task<std::expected<void, std::error_code>>
{
    request outgoing{input.method,
        append_upstream_uri(upstream_, input.path)};
    copy_http3_request(input, outgoing, options_);
    if (input.body_source)
        outgoing.set_body_source(std::move(input.body_source));
    else if (!input.body.empty())
        outgoing.set_body(std::move(input.body));

    cancel_token acquire_cancellation;
    cancellation_link acquire_link{cancellation, acquire_cancellation};
    auto lease = options_.pool.acquire_timeout >
            std::chrono::milliseconds::zero()
        ? co_await with_timeout(context_, options_.pool.acquire_timeout,
              pool_.acquire(acquire_cancellation), acquire_cancellation)
        : co_await pool_.acquire(acquire_cancellation);
    if (!lease)
    {
        if (cancellation.is_cancelled())
            co_return std::unexpected(
                std::make_error_code(std::errc::operation_canceled));
        set_http3_error(output,
            is_timeout(lease.error()) ? status::gateway_timeout
                                      : status::service_unavailable,
            is_timeout(lease.error()) ? "upstream lease timed out"
                                      : "upstream capacity unavailable");
        co_return {};
    }

    cancel_token send_cancellation;
    cancellation_link send_link{cancellation, send_cancellation};
    auto result = options_.pool.client.request_timeout >
            std::chrono::milliseconds::zero()
        ? co_await with_timeout(context_,
              options_.pool.client.request_timeout,
              lease->get().send(outgoing, send_cancellation),
              send_cancellation)
        : co_await lease->get().send(outgoing, send_cancellation);
    if (!result)
    {
        lease->discard();
        if (cancellation.is_cancelled())
            co_return std::unexpected(
                std::make_error_code(std::errc::operation_canceled));
        set_http3_error(output,
            is_timeout(result.error()) ? status::gateway_timeout
                                       : status::bad_gateway,
            is_timeout(result.error()) ? "upstream request timed out"
                                       : "upstream request failed");
        co_return {};
    }
    transfer_http3_response(*result, output, input.method);
    co_return {};
}
#endif

auto reverse_proxy::close() -> task<void>
{
    co_await pool_.close();
}

auto reverse_proxy::pool() noexcept -> bounded_client_pool&
{
    return pool_;
}

} // namespace cnetmod::http
