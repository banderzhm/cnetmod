module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.http.reverse_proxy;

import std;
import cnetmod.coro.cancel;
import cnetmod.coro.task;
import cnetmod.io.io_context;
import cnetmod.protocol.http;
import cnetmod.protocol.http.client.pool;
import cnetmod.protocol.http.semantics;

#if defined(CNETMOD_HAS_SSL) && defined(CNETMOD_ENABLE_QUIC)
import cnetmod.protocol.http.v3.session;
#endif

export namespace cnetmod::http {

/// Controls how client-supplied forwarding headers are handled.
enum class forwarded_header_policy
{
    omit,
    replace,
    append
};

struct reverse_proxy_options
{
    /// Absolute HTTP or HTTPS origin, optionally followed by a path prefix.
    std::string upstream;
    bounded_client_pool_options pool;
    forwarded_header_policy forwarded_headers =
        forwarded_header_policy::replace;
    /// Downstream scheme written to X-Forwarded-Proto. Empty omits the field.
    std::string downstream_scheme;
    bool preserve_host{};
};

/// Protocol-neutral reverse proxy for one upstream and one event loop.
///
/// Create one instance per server worker. The proxy owns a bounded upstream
/// pool, propagates the request cancellation token, strips hop-by-hop fields,
/// and transfers the upstream response into the downstream response builder.
class reverse_proxy
{
public:
    [[nodiscard]] static auto create(io_context& context,
        reverse_proxy_options options)
        -> std::expected<std::unique_ptr<reverse_proxy>, std::error_code>;

    reverse_proxy(const reverse_proxy&) = delete;
    auto operator=(const reverse_proxy&) -> reverse_proxy& = delete;

    [[nodiscard]] auto forward(request_context& context) -> task<void>;
#if defined(CNETMOD_HAS_SSL) && defined(CNETMOD_ENABLE_QUIC)
    [[nodiscard]] auto forward(v3::http3_request& request,
        v3::http3_response& response, cancel_token& cancellation)
        -> task<std::expected<void, std::error_code>>;
#endif
    [[nodiscard]] auto close() -> task<void>;

    [[nodiscard]] auto pool() noexcept -> bounded_client_pool&;

private:
    reverse_proxy(io_context& context, reverse_proxy_options options,
        std::string normalized_upstream);

    io_context& context_;
    reverse_proxy_options options_;
    std::string upstream_;
    bounded_client_pool pool_;
};

} // namespace cnetmod::http
