module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.websocket:handshake;

import std;
import :types;
import :sha1;
import :base64;
import cnetmod.protocol.http;

namespace cnetmod::ws {

// =============================================================================
// WebSocket 握手常量
// =============================================================================

namespace detail {

inline constexpr std::string_view ws_guid = "258EAFA5-E914-47DA-95CA-5AB5DC587183";

} // namespace detail

// =============================================================================
// 握手辅助函数
// =============================================================================

/// 生成随机的 Sec-WebSocket-Key（16 字节随机数的 base64）
export auto generate_sec_key() -> std::string {
    std::array<std::byte, 16> raw{};
    static thread_local std::mt19937 rng{std::random_device{}()};
    std::uniform_int_distribution<unsigned> dist(0, 255);
    for (auto& b : raw)
        b = static_cast<std::byte>(dist(rng));
    return detail::base64_encode(raw);
}

/// 计算 Sec-WebSocket-Accept 值
/// accept = base64(SHA1(key + GUID))
export auto compute_accept_key(std::string_view sec_key) -> std::string {
    std::string concat;
    concat.reserve(sec_key.size() + detail::ws_guid.size());
    concat += sec_key;
    concat += detail::ws_guid;

    auto hash = detail::sha1(concat);
    return detail::base64_encode(hash);
}

// =============================================================================
// 客户端握手
// =============================================================================

/// 构建 WebSocket 升级请求
export auto build_upgrade_request(std::string_view host, std::string_view path,
                                  std::string_view sec_key,
                                  std::string_view subprotocol = {},
                                  std::string_view origin = {})
    -> http::request
{
    http::request req(http::http_method::GET, path);
    req.set_header("Host", host);
    req.set_header("Upgrade", "websocket");
    req.set_header("Connection", "Upgrade");
    req.set_header("Sec-WebSocket-Key", sec_key);
    req.set_header("Sec-WebSocket-Version", "13");

    if (!subprotocol.empty())
        req.set_header("Sec-WebSocket-Protocol", subprotocol);
    if (!origin.empty())
        req.set_header("Origin", origin);

    return req;
}

/// 验证服务端的升级响应
export auto validate_upgrade_response(const http::response_parser& resp,
                                      std::string_view expected_accept)
    -> std::expected<void, std::error_code>
{
    if (resp.status_code() != 101)
        return std::unexpected(make_error_code(ws_errc::handshake_failed));

    // 检查 Upgrade: websocket (大小写不敏感)
    auto upgrade = resp.get_header("Upgrade");
    bool upgrade_ok = false;
    if (upgrade.size() == 9) {
        // 手动大小写不敏感比较
        std::string lower;
        lower.reserve(upgrade.size());
        for (auto c : upgrade)
            lower += static_cast<char>((c >= 'A' && c <= 'Z') ? c + 32 : c);
        upgrade_ok = (lower == "websocket");
    }
    if (!upgrade_ok)
        return std::unexpected(make_error_code(ws_errc::handshake_failed));

    // 检查 Connection: Upgrade
    auto connection = resp.get_header("Connection");
    bool conn_ok = false;
    if (!connection.empty()) {
        std::string lower;
        lower.reserve(connection.size());
        for (auto c : connection)
            lower += static_cast<char>((c >= 'A' && c <= 'Z') ? c + 32 : c);
        conn_ok = (lower.find("upgrade") != std::string::npos);
    }
    if (!conn_ok)
        return std::unexpected(make_error_code(ws_errc::handshake_failed));

    // 检查 Sec-WebSocket-Accept
    auto accept = resp.get_header("Sec-WebSocket-Accept");
    if (accept != expected_accept)
        return std::unexpected(make_error_code(ws_errc::handshake_failed));

    return {};
}

// =============================================================================
// 服务端握手
// =============================================================================

/// 验证客户端的 WebSocket 升级请求，返回 accept key
export auto validate_upgrade_request(const http::request_parser& req)
    -> std::expected<std::string, std::error_code>
{
    // 必须是 GET
    if (req.method() != "GET")
        return std::unexpected(make_error_code(ws_errc::handshake_failed));

    // 必须是 HTTP/1.1
    if (req.version() != http::http_version::http_1_1)
        return std::unexpected(make_error_code(ws_errc::handshake_failed));

    // Upgrade: websocket
    auto upgrade = req.get_header("Upgrade");
    {
        std::string lower;
        lower.reserve(upgrade.size());
        for (auto c : upgrade)
            lower += static_cast<char>((c >= 'A' && c <= 'Z') ? c + 32 : c);
        if (lower != "websocket")
            return std::unexpected(make_error_code(ws_errc::handshake_failed));
    }

    // Connection 包含 "Upgrade"
    auto connection = req.get_header("Connection");
    {
        std::string lower;
        lower.reserve(connection.size());
        for (auto c : connection)
            lower += static_cast<char>((c >= 'A' && c <= 'Z') ? c + 32 : c);
        if (lower.find("upgrade") == std::string::npos)
            return std::unexpected(make_error_code(ws_errc::handshake_failed));
    }

    // Sec-WebSocket-Key
    auto sec_key = req.get_header("Sec-WebSocket-Key");
    if (sec_key.empty())
        return std::unexpected(make_error_code(ws_errc::handshake_failed));

    // Sec-WebSocket-Version: 13
    auto version = req.get_header("Sec-WebSocket-Version");
    if (version != "13")
        return std::unexpected(make_error_code(ws_errc::handshake_failed));

    return compute_accept_key(sec_key);
}

/// 构建服务端的 WebSocket 升级响应
export auto build_upgrade_response(std::string_view accept_key,
                                   std::string_view subprotocol = {})
    -> http::response
{
    http::response resp(http::status::switching_protocols);
    resp.set_header("Upgrade", "websocket");
    resp.set_header("Connection", "Upgrade");
    resp.set_header("Sec-WebSocket-Accept", accept_key);

    if (!subprotocol.empty())
        resp.set_header("Sec-WebSocket-Protocol", subprotocol);

    return resp;
}

} // namespace cnetmod::ws
