module;

#include <cnetmod/config.hpp>
#include <cstring>

#ifdef CNETMOD_PLATFORM_WINDOWS
#include <WinSock2.h>
#include <WS2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#endif

export module cnetmod.core.address;

import std;

namespace cnetmod {

// =============================================================================
// IP 地址
// =============================================================================

/// IP 地址族
export enum class address_family {
    ipv4,
    ipv6,
    unspecified,
};

/// IPv4 地址
export class ipv4_address {
public:
    constexpr ipv4_address() noexcept : addr_{} {}

    /// 从 4 字节构造
    constexpr ipv4_address(std::uint8_t a, std::uint8_t b,
                           std::uint8_t c, std::uint8_t d) noexcept
        : bytes_{a, b, c, d} {}

    /// 从字符串解析
    [[nodiscard]] static auto from_string(std::string_view str)
        -> std::expected<ipv4_address, std::error_code>;

    /// 转换为字符串
    [[nodiscard]] auto to_string() const -> std::string;

    /// 是否为回环地址 (127.0.0.0/8)
    [[nodiscard]] constexpr auto is_loopback() const noexcept -> bool {
        return bytes_[0] == 127;
    }

    /// 是否为任意地址 (0.0.0.0)
    [[nodiscard]] constexpr auto is_any() const noexcept -> bool {
        return addr_.s_addr == 0;
    }

    /// 获取底层原生地址
    [[nodiscard]] auto native() const noexcept -> const ::in_addr& {
        return addr_;
    }

    /// 回环地址 127.0.0.1
    [[nodiscard]] static constexpr auto loopback() noexcept -> ipv4_address {
        return {127, 0, 0, 1};
    }

    /// 任意地址 0.0.0.0
    [[nodiscard]] static constexpr auto any() noexcept -> ipv4_address {
        return {};
    }

    auto operator==(const ipv4_address& rhs) const noexcept -> bool {
        return std::memcmp(&addr_, &rhs.addr_, sizeof(addr_)) == 0;
    }
    auto operator!=(const ipv4_address& rhs) const noexcept -> bool {
        return !(*this == rhs);
    }

private:
    union {
        ::in_addr addr_;
        std::uint8_t bytes_[4];
    };
};

/// IPv6 地址
export class ipv6_address {
public:
    constexpr ipv6_address() noexcept : addr_{} {}

    /// 从字符串解析
    [[nodiscard]] static auto from_string(std::string_view str)
        -> std::expected<ipv6_address, std::error_code>;

    /// 转换为字符串
    [[nodiscard]] auto to_string() const -> std::string;

    /// 是否为回环地址 (::1)
    [[nodiscard]] auto is_loopback() const noexcept -> bool;

    /// 获取底层原生地址
    [[nodiscard]] auto native() const noexcept -> const ::in6_addr& {
        return addr_;
    }

    /// 从原生 in6_addr 构造
    [[nodiscard]] static auto from_native(const ::in6_addr& native_addr) noexcept
        -> ipv6_address
    {
        ipv6_address result;
        result.addr_ = native_addr;
        return result;
    }

    /// 回环地址 ::1
    [[nodiscard]] static auto loopback() noexcept -> ipv6_address;

    /// 任意地址 ::
    [[nodiscard]] static constexpr auto any() noexcept -> ipv6_address {
        return {};
    }

    auto operator==(const ipv6_address& rhs) const noexcept -> bool {
        return std::memcmp(&addr_, &rhs.addr_, sizeof(addr_)) == 0;
    }
    auto operator!=(const ipv6_address& rhs) const noexcept -> bool {
        return !(*this == rhs);
    }

private:
    ::in6_addr addr_;
};

/// 通用 IP 地址（IPv4 或 IPv6）
export class ip_address {
public:
    ip_address() noexcept : v4_{}, family_(address_family::ipv4) {}
    ip_address(ipv4_address addr) noexcept : v4_(addr), family_(address_family::ipv4) {}
    ip_address(ipv6_address addr) noexcept : v6_(addr), family_(address_family::ipv6) {}

    /// 从字符串自动检测并解析
    [[nodiscard]] static auto from_string(std::string_view str)
        -> std::expected<ip_address, std::error_code>;

    [[nodiscard]] auto to_string() const -> std::string;
    [[nodiscard]] auto family() const noexcept -> address_family { return family_; }
    [[nodiscard]] auto is_v4() const noexcept -> bool { return family_ == address_family::ipv4; }
    [[nodiscard]] auto is_v6() const noexcept -> bool { return family_ == address_family::ipv6; }

    [[nodiscard]] auto to_v4() const -> const ipv4_address& { return v4_; }
    [[nodiscard]] auto to_v6() const -> const ipv6_address& { return v6_; }

private:
    union {
        ipv4_address v4_;
        ipv6_address v6_;
    };
    address_family family_;
};

// =============================================================================
// 端点（地址 + 端口）
// =============================================================================

/// 网络端点 = IP 地址 + 端口号
export class endpoint {
public:
    endpoint() noexcept = default;
    endpoint(ip_address addr, std::uint16_t port) noexcept
        : address_(addr), port_(port) {}

    [[nodiscard]] auto address() const noexcept -> const ip_address& { return address_; }
    [[nodiscard]] auto port() const noexcept -> std::uint16_t { return port_; }

    void set_address(ip_address addr) noexcept { address_ = addr; }
    void set_port(std::uint16_t p) noexcept { port_ = p; }

    [[nodiscard]] auto to_string() const -> std::string;

private:
    ip_address address_;
    std::uint16_t port_ = 0;
};

} // namespace cnetmod
