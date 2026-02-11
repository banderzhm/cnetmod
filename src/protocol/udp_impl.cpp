module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.udp;

namespace cnetmod::udp {

auto udp_socket::open(const endpoint& ep, const socket_options& opts)
    -> std::expected<void, std::error_code>
{
    auto family = ep.address().is_v6() ? address_family::ipv6 : address_family::ipv4;
    auto sock = socket::create(family, socket_type::datagram);
    if (!sock) return std::unexpected(sock.error());

    if (auto r = sock->apply_options(opts); !r) return r;
    if (auto r = sock->bind(ep); !r) return r;

    socket_ = std::move(*sock);
    return {};
}

auto udp_socket::open(address_family family)
    -> std::expected<void, std::error_code>
{
    auto sock = socket::create(family, socket_type::datagram);
    if (!sock) return std::unexpected(sock.error());

    socket_ = std::move(*sock);
    return {};
}

} // namespace cnetmod::udp
