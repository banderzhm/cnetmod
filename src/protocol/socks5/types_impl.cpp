module;

module cnetmod.protocol.socks5;

import std;

namespace cnetmod::socks5 {

// =============================================================================
// socks5_address Implementation
// =============================================================================

auto socks5_address::serialize() const -> std::vector<std::byte> {
    std::vector<std::byte> result;
    
    result.push_back(static_cast<std::byte>(type));
    
    switch (type) {
    case address_type::ipv4: {
        // Parse IPv4 address (e.g., "192.168.1.1")
        std::istringstream iss(host);
        std::string segment;
        while (std::getline(iss, segment, '.')) {
            result.push_back(static_cast<std::byte>(std::stoi(segment)));
        }
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
        // IPv6 address (16 bytes)
        // Simplified: assume host is already in binary format or needs parsing
        // For now, just copy the bytes
        for (std::size_t i = 0; i < 16 && i < host.size(); ++i) {
            result.push_back(static_cast<std::byte>(host[i]));
        }
        break;
    }
    }
    
    // Port (big-endian)
    result.push_back(static_cast<std::byte>(port >> 8));
    result.push_back(static_cast<std::byte>(port & 0xFF));
    
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
            static_cast<int>(data[offset]),
            static_cast<int>(data[offset + 1]),
            static_cast<int>(data[offset + 2]),
            static_cast<int>(data[offset + 3]));
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
        
        // Format IPv6 address
        std::ostringstream oss;
        for (int i = 0; i < 16; i += 2) {
            if (i > 0) oss << ":";
            oss << std::hex << std::setw(2) << std::setfill('0')
                << static_cast<int>(data[offset + i])
                << std::setw(2) << std::setfill('0')
                << static_cast<int>(data[offset + i + 1]);
        }
        addr.host = oss.str();
        offset += 16;
        break;
    }
    default:
        return std::nullopt;
    }
    
    // Parse port (big-endian)
    if (len < offset + 2) return std::nullopt;
    addr.port = (static_cast<std::uint16_t>(data[offset]) << 8) |
                 static_cast<std::uint16_t>(data[offset + 1]);
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

} // namespace cnetmod::socks5
