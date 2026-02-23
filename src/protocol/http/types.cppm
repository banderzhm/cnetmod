module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.http:types;

import std;

namespace cnetmod::http {

// =============================================================================
// HTTP Method
// =============================================================================

export enum class http_method {
    GET,
    HEAD,
    POST,
    PUT,
    DELETE_,   // DELETE is a Windows macro
    CONNECT,
    OPTIONS,
    TRACE,
    PATCH,
};

export constexpr auto method_to_string(http_method m) noexcept -> std::string_view {
    switch (m) {
        case http_method::GET:     return "GET";
        case http_method::HEAD:    return "HEAD";
        case http_method::POST:    return "POST";
        case http_method::PUT:     return "PUT";
        case http_method::DELETE_: return "DELETE";
        case http_method::CONNECT: return "CONNECT";
        case http_method::OPTIONS: return "OPTIONS";
        case http_method::TRACE:   return "TRACE";
        case http_method::PATCH:   return "PATCH";
        default:                   return "UNKNOWN";
    }
}

export constexpr auto string_to_method(std::string_view s) noexcept
    -> std::optional<http_method>
{
    if (s == "GET")     return http_method::GET;
    if (s == "HEAD")    return http_method::HEAD;
    if (s == "POST")    return http_method::POST;
    if (s == "PUT")     return http_method::PUT;
    if (s == "DELETE")  return http_method::DELETE_;
    if (s == "CONNECT") return http_method::CONNECT;
    if (s == "OPTIONS") return http_method::OPTIONS;
    if (s == "TRACE")   return http_method::TRACE;
    if (s == "PATCH")   return http_method::PATCH;
    return std::nullopt;
}

// =============================================================================
// HTTP Version
// =============================================================================

export enum class http_version {
    http_1_0,
    http_1_1,
    http_2,     // HTTP/2 (RFC 9113)
    http_3,     // HTTP/3 (RFC 9114) — reserved for future
};

export constexpr auto version_to_string(http_version v) noexcept -> std::string_view {
    switch (v) {
        case http_version::http_1_0: return "HTTP/1.0";
        case http_version::http_1_1: return "HTTP/1.1";
        case http_version::http_2:   return "HTTP/2";
        case http_version::http_3:   return "HTTP/3";
        default:                     return "HTTP/1.1";
    }
}

export constexpr auto string_to_version(std::string_view s) noexcept
    -> std::optional<http_version>
{
    if (s == "HTTP/1.0") return http_version::http_1_0;
    if (s == "HTTP/1.1") return http_version::http_1_1;
    if (s == "HTTP/2" || s == "h2") return http_version::http_2;
    if (s == "HTTP/3" || s == "h3") return http_version::http_3;
    return std::nullopt;
}

// =============================================================================
// HTTP Status Code
// =============================================================================

export namespace status {
    // 1xx
    inline constexpr int continue_             = 100;
    inline constexpr int switching_protocols    = 101;

    // 2xx
    inline constexpr int ok                    = 200;
    inline constexpr int created               = 201;
    inline constexpr int accepted              = 202;
    inline constexpr int no_content            = 204;
    inline constexpr int partial_content       = 206;

    // 3xx
    inline constexpr int moved_permanently     = 301;
    inline constexpr int found                 = 302;
    inline constexpr int see_other             = 303;
    inline constexpr int not_modified          = 304;
    inline constexpr int temporary_redirect    = 307;
    inline constexpr int permanent_redirect    = 308;

    // 4xx
    inline constexpr int bad_request           = 400;
    inline constexpr int unauthorized          = 401;
    inline constexpr int forbidden             = 403;
    inline constexpr int not_found             = 404;
    inline constexpr int method_not_allowed    = 405;
    inline constexpr int request_timeout       = 408;
    inline constexpr int conflict              = 409;
    inline constexpr int gone                  = 410;
    inline constexpr int length_required       = 411;
    inline constexpr int payload_too_large     = 413;
    inline constexpr int uri_too_long          = 414;
    inline constexpr int unsupported_media_type = 415;
    inline constexpr int upgrade_required      = 426;
    inline constexpr int too_many_requests     = 429;

    // 5xx
    inline constexpr int internal_server_error = 500;
    inline constexpr int not_implemented       = 501;
    inline constexpr int bad_gateway           = 502;
    inline constexpr int service_unavailable   = 503;
    inline constexpr int gateway_timeout       = 504;
}

export constexpr auto status_reason(int code) noexcept -> std::string_view {
    switch (code) {
        case 100: return "Continue";
        case 101: return "Switching Protocols";
        case 200: return "OK";
        case 201: return "Created";
        case 202: return "Accepted";
        case 204: return "No Content";
        case 206: return "Partial Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 303: return "See Other";
        case 304: return "Not Modified";
        case 307: return "Temporary Redirect";
        case 308: return "Permanent Redirect";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 409: return "Conflict";
        case 410: return "Gone";
        case 411: return "Length Required";
        case 413: return "Payload Too Large";
        case 414: return "URI Too Long";
        case 415: return "Unsupported Media Type";
        case 426: return "Upgrade Required";
        case 429: return "Too Many Requests";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        case 504: return "Gateway Timeout";
        default:  return "Unknown";
    }
}

// =============================================================================
// Case-Insensitive Header Map
// =============================================================================

export struct case_insensitive_less {
    auto operator()(std::string_view a, std::string_view b) const noexcept -> bool {
        auto len = std::min(a.size(), b.size());
        for (std::size_t i = 0; i < len; ++i) {
            auto ca = static_cast<unsigned char>(a[i]);
            auto cb = static_cast<unsigned char>(b[i]);
            auto la = (ca >= 'A' && ca <= 'Z') ? ca + 32 : ca;
            auto lb = (cb >= 'A' && cb <= 'Z') ? cb + 32 : cb;
            if (la < lb) return true;
            if (la > lb) return false;
        }
        return a.size() < b.size();
    }
};

export using header_map = std::map<std::string, std::string, case_insensitive_less>;

// =============================================================================
// Simple URL Structure
// =============================================================================

export struct url {
    std::string scheme;    // "http", "https", "ws", "wss"
    std::string host;
    std::uint16_t port = 0;
    std::string path;      // With leading '/'
    std::string query;     // Without '?'
    std::string fragment;  // Without '#'

    /// Parse URL string
    [[nodiscard]] static auto parse(std::string_view input)
        -> std::expected<url, std::string>
    {
        url u;
        auto pos = input.find("://");
        if (pos == std::string_view::npos)
            return std::unexpected(std::string("missing scheme"));

        u.scheme = std::string(input.substr(0, pos));
        auto rest = input.substr(pos + 3);

        // fragment
        auto frag = rest.find('#');
        if (frag != std::string_view::npos) {
            u.fragment = std::string(rest.substr(frag + 1));
            rest = rest.substr(0, frag);
        }

        // query
        auto qm = rest.find('?');
        if (qm != std::string_view::npos) {
            u.query = std::string(rest.substr(qm + 1));
            rest = rest.substr(0, qm);
        }

        // path
        auto slash = rest.find('/');
        if (slash != std::string_view::npos) {
            u.path = std::string(rest.substr(slash));
            rest = rest.substr(0, slash);
        } else {
            u.path = "/";
        }

        // host:port
        // Handle IPv6 [::1]:port
        if (!rest.empty() && rest[0] == '[') {
            auto bracket = rest.find(']');
            if (bracket == std::string_view::npos)
                return std::unexpected(std::string("unclosed IPv6 bracket"));
            u.host = std::string(rest.substr(1, bracket - 1));
            rest = rest.substr(bracket + 1);
            if (!rest.empty() && rest[0] == ':') {
                auto port_str = rest.substr(1);
                auto [ptr, ec] = std::from_chars(port_str.data(), port_str.data() + port_str.size(), u.port);
                if (ec != std::errc{})
                    return std::unexpected(std::string("invalid port"));
            }
        } else {
            auto colon = rest.rfind(':');
            if (colon != std::string_view::npos) {
                u.host = std::string(rest.substr(0, colon));
                auto port_str = rest.substr(colon + 1);
                auto [ptr, ec] = std::from_chars(port_str.data(), port_str.data() + port_str.size(), u.port);
                if (ec != std::errc{})
                    return std::unexpected(std::string("invalid port"));
            } else {
                u.host = std::string(rest);
            }
        }

        // Default port
        if (u.port == 0) {
            if (u.scheme == "http" || u.scheme == "ws")   u.port = 80;
            if (u.scheme == "https" || u.scheme == "wss") u.port = 443;
        }

        return u;
    }
};

// =============================================================================
// HTTP Limit Constants
// =============================================================================

export inline constexpr std::size_t max_header_size = 16 * 1024;  // 16 KB
export inline constexpr std::size_t max_body_size   = 32 * 1024 * 1024; // 32 MB

// =============================================================================
// HTTP Error Codes
// =============================================================================

export enum class http_errc {
    success = 0,
    invalid_method,
    invalid_uri,
    invalid_version,
    invalid_status_line,
    invalid_header,
    header_too_large,
    body_too_large,
    incomplete_message,
    need_more_data,
    invalid_chunk,
    invalid_multipart,
    missing_boundary,
    invalid_content_disposition,
    unsupported_encoding,
    invalid_url_encoding,
};

namespace detail {

class http_error_category_impl : public std::error_category {
public:
    auto name() const noexcept -> const char* override { return "http"; }
    auto message(int ev) const -> std::string override {
        switch (static_cast<http_errc>(ev)) {
            case http_errc::success:            return "success";
            case http_errc::invalid_method:     return "invalid HTTP method";
            case http_errc::invalid_uri:        return "invalid URI";
            case http_errc::invalid_version:    return "invalid HTTP version";
            case http_errc::invalid_status_line:return "invalid status line";
            case http_errc::invalid_header:     return "invalid header";
            case http_errc::header_too_large:   return "header too large";
            case http_errc::body_too_large:     return "body too large";
            case http_errc::incomplete_message: return "incomplete message";
            case http_errc::need_more_data:     return "need more data";
            case http_errc::invalid_chunk:      return "invalid chunk encoding";
            case http_errc::invalid_multipart:   return "invalid multipart data";
            case http_errc::missing_boundary:     return "missing multipart boundary";
            case http_errc::invalid_content_disposition: return "invalid Content-Disposition";
            case http_errc::unsupported_encoding: return "unsupported Content-Transfer-Encoding";
            case http_errc::invalid_url_encoding: return "invalid URL encoding";
            default:                            return "unknown http error";
        }
    }
};

inline auto http_category_instance() -> const std::error_category& {
    static const http_error_category_impl instance;
    return instance;
}

} // namespace detail

export inline auto make_error_code(http_errc e) noexcept -> std::error_code {
    return {static_cast<int>(e), detail::http_category_instance()};
}

} // namespace cnetmod::http

template <>
struct std::is_error_code_enum<cnetmod::http::http_errc> : std::true_type {};
