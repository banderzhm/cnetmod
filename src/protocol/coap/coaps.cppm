/// cnetmod.protocol.coap:coaps - CoAP over DTLS (CoAPS) interface.

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.coap:coaps;

#ifdef CNETMOD_HAS_SSL

import std;
import :types;
import :server;
import :coaps_security;
import :coaps_session_manager;
import cnetmod.core.address;
import cnetmod.core.socket;
import cnetmod.core.ssl;
import cnetmod.io.io_context;
import cnetmod.coro.task;

namespace cnetmod::coap {

export struct secure_client_config {
    client_config coap;
    std::size_t dtls_mtu = 1400;
    coaps_security_config security;
    std::chrono::seconds handshake_timeout{10};
};

export struct secure_server_config {
    server_config coap;
    std::size_t dtls_mtu = 1400;
    std::size_t max_sessions = 1024;
    std::size_t session_queue_capacity = 64;
    std::chrono::seconds session_idle_timeout{120};
    coaps_security_config client_security;
    std::chrono::seconds handshake_timeout{10};
};

export class secure_client {
public:
    explicit secure_client(io_context& ctx, ssl_context& ssl_ctx,
                           secure_client_config cfg = {});

    secure_client(const secure_client&) = delete;
    auto operator=(const secure_client&) -> secure_client& = delete;

    auto request(const endpoint& remote, message req)
        -> task<std::expected<message, std::error_code>>;

    auto get(const endpoint& remote, std::string path, std::string query = {})
        -> task<std::expected<message, std::error_code>>;

    auto post(const endpoint& remote, std::string path, std::vector<std::byte> payload,
              content_format format = content_format::octet_stream)
        -> task<std::expected<message, std::error_code>>;

    void close() noexcept;

private:
    auto next_message_id() -> std::uint16_t;
    auto next_token() -> std::vector<std::byte>;

    io_context& ctx_;
    ssl_context& ssl_ctx_;
    secure_client_config cfg_;
    socket sock_;
    std::atomic<std::uint16_t> message_id_{1};
    std::mt19937_64 rng_;
};

export class secure_server {
public:
    explicit secure_server(io_context& ctx, ssl_context& ssl_ctx,
                           secure_server_config cfg = {});

    secure_server(const secure_server&) = delete;
    auto operator=(const secure_server&) -> secure_server& = delete;

    auto listen(std::string_view host, std::uint16_t port = default_secure_port,
                socket_options opts = {.reuse_address = true, .non_blocking = true})
        -> std::expected<void, std::error_code>;

    void set_handler(request_handler handler);
    void set_etag_provider(etag_provider provider);
    void route(method m, std::string path, request_handler handler);

    auto run() -> task<void>;
    void stop() noexcept;
    ~secure_server();

private:
    auto check_preconditions(const inbound_request& req) const -> std::optional<message>;
    auto dispatch_request(const inbound_request& req, const endpoint& peer) -> task<message>;
    void refresh_session_handler();

    io_context& ctx_;
    ssl_context& ssl_ctx_;
    secure_server_config cfg_;
    socket sock_;
    bool running_ = false;
    request_handler handler_;
    etag_provider etag_provider_;
    resource_router router_;
    std::unique_ptr<coaps_session_manager> sessions_;
};

} // namespace cnetmod::coap

#endif // CNETMOD_HAS_SSL
