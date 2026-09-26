module;

#include <cnetmod/config.hpp>
#ifdef CNETMOD_PLATFORM_WINDOWS
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #include <WS2tcpip.h>
    #include <WinSock2.h>
#else
    #include <arpa/inet.h>
    #include <netinet/in.h>
    #include <sys/socket.h>
#endif

module cnetmod.protocol.http;
import std;
import :utils;
import :router;

namespace cnetmod::http {
namespace {
    auto trim(std::string_view value) noexcept -> std::string_view
    {
        while (!value.empty() &&
            std::isspace(static_cast<unsigned char>(value.front())))
            value.remove_prefix(1);
        while (!value.empty() &&
            std::isspace(static_cast<unsigned char>(value.back())))
            value.remove_suffix(1);
        return value;
    }

    auto strip_ipv6_brackets(std::string_view value) noexcept -> std::string_view
    {
        value = trim(value);
        return value.size() >= 2 && value.front() == '[' && value.back() == ']'
            ? value.substr(1, value.size() - 2)
            : value;
    }

    template <std::size_t Size>
    auto parse_address(std::string_view value, int family)
        -> std::optional<std::array<std::uint8_t, Size>>
    {
        value = strip_ipv6_brackets(value);
        std::array<std::uint8_t, Size> address{};
        const auto text = std::string(value);
        return ::inet_pton(family, text.c_str(), address.data()) == 1
            ? std::optional{address}
            : std::nullopt;
    }

    auto parse_prefix(std::string_view value, int max_bits) -> std::optional<int>
    {
        value = trim(value);
        int bits{};
        const auto [end, error] =
            std::from_chars(value.data(), value.data() + value.size(), bits);
        return !value.empty() && error == std::errc{} &&
                end == value.data() + value.size() && bits >= 0 &&
                bits <= max_bits
            ? std::optional{bits}
            : std::nullopt;
    }

    template <std::size_t Size>
    auto prefix_matches(const std::array<std::uint8_t, Size>& client,
        const std::array<std::uint8_t, Size>& network,
        int bits) noexcept -> bool
    {
        const auto full_bytes = static_cast<std::size_t>(bits / 8);
        for (std::size_t index = 0; index < full_bytes; ++index)
            if (client[index] != network[index])
                return false;
        const auto remainder = bits % 8;
        if (remainder == 0)
            return true;
        const auto mask = static_cast<std::uint8_t>(0xffu << (8 - remainder));
        return (client[full_bytes] & mask) == (network[full_bytes] & mask);
    }

    auto address_matches(std::string_view client, std::string_view pattern) -> bool
    {
        client = strip_ipv6_brackets(client);
        pattern = strip_ipv6_brackets(pattern);
        const auto slash = pattern.find('/');
        if (slash == std::string_view::npos)
        {
            const auto client_v4 = parse_address<4>(client, AF_INET);
            const auto pattern_v4 = parse_address<4>(pattern, AF_INET);
            if (client_v4 && pattern_v4)
                return *client_v4 == *pattern_v4;
            const auto client_v6 = parse_address<16>(client, AF_INET6);
            const auto pattern_v6 = parse_address<16>(pattern, AF_INET6);
            return client_v6 && pattern_v6 && *client_v6 == *pattern_v6;
        }
        const auto network = strip_ipv6_brackets(pattern.substr(0, slash));
        const auto prefix = pattern.substr(slash + 1);
        if (const auto client_v4 = parse_address<4>(client, AF_INET))
        {
            const auto network_v4 = parse_address<4>(network, AF_INET);
            const auto bits = parse_prefix(prefix, 32);
            return network_v4 && bits && prefix_matches(*client_v4, *network_v4, *bits);
        }
        if (const auto client_v6 = parse_address<16>(client, AF_INET6))
        {
            const auto network_v6 = parse_address<16>(network, AF_INET6);
            const auto bits = parse_prefix(prefix, 128);
            return network_v6 && bits && prefix_matches(*client_v6, *network_v6, *bits);
        }
        return false;
    }

} // namespace

auto ip_matches(std::string_view address, std::string_view pattern) -> bool
{
    return address_matches(address, pattern);
}

namespace {
    auto normalized_address(std::string_view address)
        -> std::optional<std::string>
    {
        address = strip_ipv6_brackets(address);
        if (parse_address<4>(address, AF_INET) ||
            parse_address<16>(address, AF_INET6))
            return std::string{address};
        return std::nullopt;
    }

    auto trusted(std::string_view address, std::span<const std::string> proxies)
        -> bool
    {
        return std::ranges::any_of(proxies,
            [address](const std::string& pattern)
            {
                return address_matches(address, pattern);
            });
    }
} // namespace

auto resolve_forwarded_client_ip(std::string_view peer_address,
    std::string_view x_forwarded_for, std::string_view x_real_ip,
    std::span<const std::string> trusted_proxies) -> std::string
{
    const auto peer = normalized_address(peer_address);
    if (!peer)
        return "unknown";
    if (!trusted(*peer, trusted_proxies))
        return *peer;

    // Walk right to left: every hop appended by a trusted proxy is skipped,
    // the first untrusted hop is the client. Entries left of it are
    // client-supplied and ignored.
    std::string_view remaining = x_forwarded_for;
    std::optional<std::string> furthest;
    while (!remaining.empty())
    {
        const auto comma = remaining.rfind(',');
        const auto hop = trim(comma == std::string_view::npos
                ? remaining
                : remaining.substr(comma + 1));
        remaining = comma == std::string_view::npos
            ? std::string_view{}
            : remaining.substr(0, comma);
        const auto normalized = normalized_address(hop);
        if (!normalized)
            return *peer;
        furthest = *normalized;
        if (!trusted(*normalized, trusted_proxies))
            return *normalized;
    }
    if (furthest)
        return *furthest;
    if (const auto real = normalized_address(x_real_ip))
        return *real;
    return *peer;
}

auto resolve_client_ip(const request_context& request,
    std::span<const std::string> trusted_proxies) -> std::string
{
    return resolve_forwarded_client_ip(request.peer_address(),
        request.get_header("X-Forwarded-For"), request.get_header("X-Real-IP"),
        trusted_proxies);
}

auto parse_query_param(std::string_view q, std::string_view key) -> std::string
{
    for (std::size_t p = 0; p < q.size();)
    {
        auto a = q.find('&', p);
        auto s = q.substr(p, a == std::string_view::npos ? q.size() - p : a - p);
        auto e = s.find('=');
        if (e != std::string_view::npos && s.substr(0, e) == key)
            return std::string(s.substr(e + 1));
        if (a == std::string_view::npos)
            break;
        p = a + 1;
    }
    return {};
}

auto parse_query_params(std::string_view q)
    -> cnetmod::flat_map<std::string, std::string>
{
    cnetmod::flat_map<std::string, std::string> r;
    for (std::size_t p = 0; p < q.size();)
    {
        auto a = q.find('&', p);
        auto s = q.substr(p, a == std::string_view::npos ? q.size() - p : a - p);
        auto e = s.find('=');
        if (e != std::string_view::npos)
            r.insert_or_assign(std::string(s.substr(0, e)), std::string(s.substr(e + 1)));
        if (a == std::string_view::npos)
            break;
        p = a + 1;
    }
    return r;
}
} // namespace cnetmod::http
