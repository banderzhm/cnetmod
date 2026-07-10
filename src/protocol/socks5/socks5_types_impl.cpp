module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_PLATFORM_WINDOWS
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <WinSock2.h>
#include <WS2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#endif

module cnetmod.protocol.socks5;

import std;
import :types;

namespace cnetmod::socks5 {

namespace {

auto put_network_port(std::vector<std::byte>& out, std::uint16_t port) -> void {
    out.push_back(static_cast<std::byte>((port >> 8) & 0xFF));
    out.push_back(static_cast<std::byte>(port & 0xFF));
}

auto byte_to_u8(std::byte b) noexcept -> std::uint8_t {
    return std::to_integer<std::uint8_t>(b);
}

} // namespace

// =============================================================================
// socks5_address Implementation
// =============================================================================

auto socks5_address::serialize() const -> std::vector<std::byte> {
    std::vector<std::byte> result;
    
    result.push_back(static_cast<std::byte>(type));
    
    switch (type) {
    case address_type::ipv4: {
        ::in_addr addr{};
        if (::inet_pton(AF_INET, host.c_str(), &addr) != 1) {
            addr.s_addr = 0;
        }
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(&addr);
        result.insert(result.end(),
            reinterpret_cast<const std::byte*>(bytes),
            reinterpret_cast<const std::byte*>(bytes + 4));
        break;
    }
    case address_type::domain_name: {
        // Domain name: length + name
        result.push_back(static_cast<std::byte>(host.size()));
        for (char ch : host) {
            result.push_back(static_cast<std::byte>(ch));
        }
        break;
    }
    case address_type::ipv6: {
        ::in6_addr addr{};
        if (::inet_pton(AF_INET6, host.c_str(), &addr) != 1) {
            addr = {};
        }
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(&addr);
        result.insert(result.end(),
            reinterpret_cast<const std::byte*>(bytes),
            reinterpret_cast<const std::byte*>(bytes + 16));
        break;
    }
    }
    
    put_network_port(result, port);
    
    return result;
}

auto socks5_address::parse(const std::byte* data, std::size_t len) 
    -> std::optional<std::pair<socks5_address, std::size_t>> {
    
    if (len < 1) return std::nullopt;
    
    socks5_address addr;
    std::size_t offset = 0;
    
    addr.type = static_cast<address_type>(data[offset++]);
    
    switch (addr.type) {
    case address_type::ipv4: {
        if (len < offset + 4 + 2) return std::nullopt;
        
        addr.host = std::format("{}.{}.{}.{}",
            static_cast<int>(byte_to_u8(data[offset])),
            static_cast<int>(byte_to_u8(data[offset + 1])),
            static_cast<int>(byte_to_u8(data[offset + 2])),
            static_cast<int>(byte_to_u8(data[offset + 3])));
        offset += 4;
        break;
    }
    case address_type::domain_name: {
        if (len < offset + 1) return std::nullopt;
        
        std::uint8_t name_len = static_cast<std::uint8_t>(data[offset++]);
        if (len < offset + name_len + 2) return std::nullopt;
        
        addr.host.resize(name_len);
        for (std::uint8_t i = 0; i < name_len; ++i) {
            addr.host[i] = static_cast<char>(data[offset++]);
        }
        break;
    }
    case address_type::ipv6: {
        if (len < offset + 16 + 2) return std::nullopt;
        
        char buf[INET6_ADDRSTRLEN]{};
        ::in6_addr native{};
        std::memcpy(&native, data + offset, sizeof(native));
        if (::inet_ntop(AF_INET6, &native, buf, sizeof(buf)) == nullptr) {
            return std::nullopt;
        }
        addr.host = buf;
        offset += 16;
        break;
    }
    default:
        return std::nullopt;
    }
    
    // Parse port (big-endian)
    if (len < offset + 2) return std::nullopt;
    addr.port = (static_cast<std::uint16_t>(byte_to_u8(data[offset])) << 8) |
                 static_cast<std::uint16_t>(byte_to_u8(data[offset + 1]));
    offset += 2;
    
    return std::make_pair(addr, offset);
}

// =============================================================================
// auth_request Implementation
// =============================================================================

auto auth_request::serialize() const -> std::vector<std::byte> {
    std::vector<std::byte> result;
    result.push_back(static_cast<std::byte>(version));
    result.push_back(static_cast<std::byte>(methods.size()));
    
    for (auto method : methods) {
        result.push_back(static_cast<std::byte>(method));
    }
    
    return result;
}

auto auth_request::parse(const std::byte* data, std::size_t len) 
    -> std::optional<auth_request> {
    
    if (len < 2) return std::nullopt;
    
    auth_request req;
    req.version = static_cast<std::uint8_t>(data[0]);
    
    if (req.version != SOCKS_VERSION) return std::nullopt;
    
    std::uint8_t method_count = static_cast<std::uint8_t>(data[1]);
    if (len < 2 + method_count) return std::nullopt;
    
    req.methods.reserve(method_count);
    for (std::uint8_t i = 0; i < method_count; ++i) {
        req.methods.push_back(static_cast<auth_method>(data[2 + i]));
    }
    
    return req;
}

// =============================================================================
// auth_response Implementation
// =============================================================================

auto auth_response::serialize() const -> std::vector<std::byte> {
    return {
        static_cast<std::byte>(version),
        static_cast<std::byte>(method)
    };
}

auto auth_response::parse(const std::byte* data, std::size_t len) 
    -> std::optional<auth_response> {
    
    if (len < 2) return std::nullopt;
    
    auth_response resp;
    resp.version = static_cast<std::uint8_t>(data[0]);
    resp.method = static_cast<auth_method>(data[1]);
    
    return resp;
}

// =============================================================================
// username_password_request Implementation
// =============================================================================

auto username_password_request::serialize() const -> std::vector<std::byte> {
    std::vector<std::byte> result;
    result.push_back(static_cast<std::byte>(version));
    
    // Username
    result.push_back(static_cast<std::byte>(username.size()));
    for (char ch : username) {
        result.push_back(static_cast<std::byte>(ch));
    }
    
    // Password
    result.push_back(static_cast<std::byte>(password.size()));
    for (char ch : password) {
        result.push_back(static_cast<std::byte>(ch));
    }
    
    return result;
}

auto username_password_request::parse(const std::byte* data, std::size_t len) 
    -> std::optional<username_password_request> {
    
    if (len < 2) return std::nullopt;
    
    username_password_request req;
    std::size_t offset = 0;
    
    req.version = static_cast<std::uint8_t>(data[offset++]);
    
    // Parse username
    std::uint8_t ulen = static_cast<std::uint8_t>(data[offset++]);
    if (len < offset + ulen + 1) return std::nullopt;
    
    req.username.resize(ulen);
    for (std::uint8_t i = 0; i < ulen; ++i) {
        req.username[i] = static_cast<char>(data[offset++]);
    }
    
    // Parse password
    std::uint8_t plen = static_cast<std::uint8_t>(data[offset++]);
    if (len < offset + plen) return std::nullopt;
    
    req.password.resize(plen);
    for (std::uint8_t i = 0; i < plen; ++i) {
        req.password[i] = static_cast<char>(data[offset++]);
    }
    
    return req;
}

// =============================================================================
// username_password_response Implementation
// =============================================================================

auto username_password_response::serialize() const -> std::vector<std::byte> {
    return {
        static_cast<std::byte>(version),
        static_cast<std::byte>(status)
    };
}

auto username_password_response::parse(const std::byte* data, std::size_t len) 
    -> std::optional<username_password_response> {
    
    if (len < 2) return std::nullopt;
    
    username_password_response resp;
    resp.version = static_cast<std::uint8_t>(data[0]);
    resp.status = static_cast<std::uint8_t>(data[1]);
    
    return resp;
}

// =============================================================================
// socks5_request Implementation
// =============================================================================

auto socks5_request::serialize() const -> std::vector<std::byte> {
    std::vector<std::byte> result;
    result.push_back(static_cast<std::byte>(version));
    result.push_back(static_cast<std::byte>(cmd));
    result.push_back(static_cast<std::byte>(reserved));
    
    auto addr_bytes = address.serialize();
    result.insert(result.end(), addr_bytes.begin(), addr_bytes.end());
    
    return result;
}

auto socks5_request::parse(const std::byte* data, std::size_t len) 
    -> std::optional<socks5_request> {
    
    if (len < 4) return std::nullopt;
    
    socks5_request req;
    req.version = static_cast<std::uint8_t>(data[0]);
    
    if (req.version != SOCKS_VERSION) return std::nullopt;
    
    req.cmd = static_cast<command>(data[1]);
    req.reserved = static_cast<std::uint8_t>(data[2]);
    
    auto addr_result = socks5_address::parse(data + 3, len - 3);
    if (!addr_result) return std::nullopt;
    
    req.address = addr_result->first;
    
    return req;
}

// =============================================================================
// socks5_response Implementation
// =============================================================================

auto socks5_response::serialize() const -> std::vector<std::byte> {
    std::vector<std::byte> result;
    result.push_back(static_cast<std::byte>(version));
    result.push_back(static_cast<std::byte>(rep));
    result.push_back(static_cast<std::byte>(reserved));
    
    auto addr_bytes = bind_address.serialize();
    result.insert(result.end(), addr_bytes.begin(), addr_bytes.end());
    
    return result;
}

auto socks5_response::parse(const std::byte* data, std::size_t len) 
    -> std::optional<socks5_response> {
    
    if (len < 4) return std::nullopt;
    
    socks5_response resp;
    resp.version = static_cast<std::uint8_t>(data[0]);
    resp.rep = static_cast<reply>(data[1]);
    resp.reserved = static_cast<std::uint8_t>(data[2]);
    
    auto addr_result = socks5_address::parse(data + 3, len - 3);
    if (!addr_result) return std::nullopt;
    
    resp.bind_address = addr_result->first;
    
    return resp;
}

// =============================================================================
// udp_datagram Implementation
// =============================================================================

auto udp_datagram::serialize() const -> std::vector<std::byte> {
    std::vector<std::byte> result;
    put_network_port(result, reserved);
    result.push_back(static_cast<std::byte>(fragment));

    auto addr_bytes = address.serialize();
    result.insert(result.end(), addr_bytes.begin(), addr_bytes.end());
    result.insert(result.end(), payload.begin(), payload.end());

    return result;
}

auto udp_datagram::parse(const std::byte* data, std::size_t len)
    -> std::optional<udp_datagram> {
    if (len < 4) return std::nullopt;

    udp_datagram result;
    result.reserved = (static_cast<std::uint16_t>(byte_to_u8(data[0])) << 8) |
                       static_cast<std::uint16_t>(byte_to_u8(data[1]));
    result.fragment = byte_to_u8(data[2]);

    auto addr_result = socks5_address::parse(data + 3, len - 3);
    if (!addr_result) return std::nullopt;

    result.address = addr_result->first;
    auto payload_offset = 3 + addr_result->second;
    if (payload_offset > len) return std::nullopt;
    result.payload.assign(data + payload_offset, data + len);
    return result;
}

} // namespace cnetmod::socks5
