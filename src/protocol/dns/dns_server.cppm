module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.dns.server;

import std;
import cnetmod.core.error;
import cnetmod.core.address;
import cnetmod.core.socket;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.protocol.udp;
import cnetmod.protocol.tcp;
import cnetmod.protocol.dns.types;
#ifdef CNETMOD_HAS_SSL
import cnetmod.core.ssl;
#endif

namespace cnetmod::dns {

export class udp_server {
public:
    explicit udp_server(io_context& ctx);

    [[nodiscard]] auto listen(std::string_view host, std::uint16_t port,
                              socket_options opts = {.reuse_address = true})
        -> std::expected<void, std::error_code>;

    void set_handler(query_handler handler);
    void stop() noexcept;

    [[nodiscard]] auto run() -> task<void>;

private:
    io_context& ctx_;
    udp::udp_socket sock_;
    query_handler handler_;
    bool running_ = false;
};

export class tcp_server {
public:
    explicit tcp_server(io_context& ctx);

    [[nodiscard]] auto listen(std::string_view host, std::uint16_t port,
                              socket_options opts = {.reuse_address = true})
        -> std::expected<void, std::error_code>;

    void set_handler(query_handler handler);
    void stop() noexcept;

    [[nodiscard]] auto run() -> task<void>;

private:
    auto handle_client(socket client) -> task<void>;

    io_context& ctx_;
    tcp::acceptor acc_;
    query_handler handler_;
    bool running_ = false;
};

#ifdef CNETMOD_HAS_SSL
export struct dot_server_options {
    std::string cert_file;
    std::string key_file;
    bool verify_peer = false;
};

export class dot_server {
public:
    dot_server(io_context& ctx, dot_server_options opts);

    [[nodiscard]] auto listen(std::string_view host, std::uint16_t port,
                              socket_options sock_opts = {.reuse_address = true})
        -> std::expected<void, std::error_code>;

    void set_handler(query_handler handler);
    void stop() noexcept;

    [[nodiscard]] auto run() -> task<void>;

private:
    auto handle_client(socket client) -> task<void>;

    io_context& ctx_;
    dot_server_options opts_;
    tcp::acceptor acc_;
    std::unique_ptr<ssl_context> ssl_ctx_;
    query_handler handler_;
    bool running_ = false;
};
#endif

} // namespace cnetmod::dns
