module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.dns.client;

import std;
import cnetmod.core.error;
import cnetmod.core.address;
import cnetmod.core.socket;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.protocol.udp;
import cnetmod.protocol.http;
import cnetmod.protocol.dns.types;
#ifdef CNETMOD_HAS_SSL
import cnetmod.core.ssl;
#endif

namespace cnetmod::dns {

export class udp_client {
public:
    explicit udp_client(io_context& ctx);

    [[nodiscard]] auto query(const endpoint& server, const message& msg)
        -> task<std::expected<message, std::error_code>>;

private:
    io_context& ctx_;
    udp::udp_socket sock_;
};

export class tcp_client {
public:
    explicit tcp_client(io_context& ctx);

    [[nodiscard]] auto query(std::string_view host, std::uint16_t port, const message& msg)
        -> task<std::expected<message, std::error_code>>;

private:
    io_context& ctx_;
    socket sock_;
};

export class doh_client {
public:
    explicit doh_client(io_context& ctx, std::string endpoint_url = "https://dns.google/dns-query");

    [[nodiscard]] auto query(const message& msg)
        -> task<std::expected<message, std::error_code>>;

private:
    http::client http_;
    std::string endpoint_url_;
};

#ifdef CNETMOD_HAS_SSL
export class dot_client {
public:
    explicit dot_client(io_context& ctx);

    [[nodiscard]] auto query(std::string_view host, std::uint16_t port, const message& msg)
        -> task<std::expected<message, std::error_code>>;

private:
    io_context& ctx_;
    socket sock_;
    std::unique_ptr<ssl_context> ssl_ctx_;
    std::unique_ptr<ssl_stream> ssl_;
};
#endif

} // namespace cnetmod::dns
