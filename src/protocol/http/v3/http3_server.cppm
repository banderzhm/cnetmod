module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_ENABLE_QUIC
    #ifdef CNETMOD_HAS_SSL

export module cnetmod.protocol.http.v3.server;

import std;
import cnetmod.core.ssl;
import cnetmod.core.address;
import cnetmod.io.io_context;
import cnetmod.executor.pool;
import cnetmod.coro.task;
import cnetmod.protocol.http.v3.session;

namespace cnetmod::http::v3 {

/// HTTP/3 UDP listener.  Its implementation owns Retry validation, CID
/// routing and socket lifetime; only this stable public contract is exported.
export class http3_server
{
public:
    http3_server(io_context& context, ssl_context& tls, endpoint listen_endpoint,
        server_request_handler handler);
    http3_server(server_context& context, ssl_context& tls,
        endpoint listen_endpoint, server_request_handler handler);
    ~http3_server();
    http3_server(const http3_server&) = delete;
    auto operator=(const http3_server&) -> http3_server& = delete;
    [[nodiscard]] auto start() -> std::expected<void, std::error_code>;
    [[nodiscard]] auto stop() -> task<void>;
    [[nodiscard]] auto is_running() const noexcept -> bool;

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};

export auto make_http3_server(io_context& ctx, ssl_context& tls, endpoint ep,
    server_request_handler handler) -> std::unique_ptr<http3_server>;
export auto make_http3_server(server_context& ctx, ssl_context& tls, endpoint ep,
    server_request_handler handler) -> std::unique_ptr<http3_server>;

} // namespace cnetmod::http::v3

    #endif
#endif
