module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.udp;

import cnetmod.io.io_context;
import cnetmod.executor.async_op;

namespace cnetmod::udp {

udp_socket::udp_socket(io_context& ctx)
    : ctx_(&ctx) {}

void udp_socket::close() noexcept
{
    socket_.close();
}

auto udp_socket::is_open() const noexcept -> bool
{
    return socket_.is_open();
}

auto udp_socket::native_socket() noexcept -> socket&
{
    return socket_;
}

auto udp_socket::context() noexcept -> io_context&
{
    return *ctx_;
}

auto udp_socket::open(const endpoint& ep, const socket_options& opts)
    -> std::expected<void, std::error_code>
{
    auto family =
        ep.address().is_v6() ? address_family::ipv6 : address_family::ipv4;
    bool registered_io{};
#ifdef CNETMOD_PLATFORM_WINDOWS
    registered_io = opts.registered_io;
#endif
    auto sock = socket::create(family, socket_type::datagram, registered_io);
    if (!sock)
        return std::unexpected(sock.error());

    if (auto r = sock->apply_options(opts); !r)
        return r;
    if (auto r = sock->bind(ep); !r)
        return r;

#ifdef CNETMOD_PLATFORM_WINDOWS
    if (registered_io)
    {
        // A RIO socket cannot safely switch to regular overlapped receives
        // after queue/buffer creation fails. Probe it while this wrapper still
        // owns the just-bound socket, then rebuild an ordinary IOCP socket if
        // the installed Winsock provider cannot supply RIO resources.
        if (auto prepared = prepare_async_datagram_io(*ctx_, *sock); !prepared)
        {
            sock->close();
            sock = socket::create(family, socket_type::datagram, false);
            if (!sock)
                return std::unexpected(sock.error());
            sock->mark_registered_io_fallback();
            if (auto r = sock->apply_options(opts); !r)
                return r;
            if (auto r = sock->bind(ep); !r)
                return r;
        }
    }
#endif

    socket_ = std::move(*sock);
    return {};
}

auto udp_socket::open(address_family family)
    -> std::expected<void, std::error_code>
{
    auto sock = socket::create(family, socket_type::datagram);
    if (!sock)
        return std::unexpected(sock.error());

    socket_ = std::move(*sock);
    return {};
}

} // namespace cnetmod::udp
