module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.http.semantics;

import std;
import cnetmod.core.buffer;
import cnetmod.coro.task;
import cnetmod.coro.cancel;
import cnetmod.coro.channel;
import cnetmod.utils.flat_map;

namespace cnetmod::http {

// =============================================================================
// Request Body Streaming
// =============================================================================

export using request_body_chunk = byte_buffer;

export class request_body_stream
{
public:
    explicit request_body_stream(std::size_t chunk_capacity = 1024);

    [[nodiscard]] auto receive() -> task<std::optional<request_body_chunk>>;

    /// Push one chunk with back-pressure. The producer suspends while the
    /// bounded stream is full and returns false after the consumer closes it.
    [[nodiscard]] auto send(request_body_chunk chunk) -> task<bool>;

    [[nodiscard]] auto try_receive() -> std::optional<request_body_chunk>;

    [[nodiscard]] auto push(request_body_chunk chunk) -> bool;

    void close() noexcept;

    [[nodiscard]] auto is_closed() const noexcept -> bool;

private:
    cnetmod::channel<request_body_chunk> chunks_;
};

/// Pull-based client request body producer.
///
/// The transport asks for the next chunk only after the previous chunk has
/// been accepted by the socket/stream.  This gives callers natural
/// back-pressure without requiring a protocol-specific send loop.  A null
/// optional marks EOF.  The supplied token is the request token and must be
/// checked by producers that perform their own asynchronous work.
export using request_body_reader =
    std::function<task<std::optional<request_body_chunk>>(cancel_token&)>;

export class request_body_source
{
public:
    request_body_source(request_body_reader reader,
        std::optional<std::uint64_t> content_length = std::nullopt)
        : reader_(std::move(reader)), content_length_(content_length) {}

    [[nodiscard]] auto next(cancel_token& token)
        -> task<std::optional<request_body_chunk>>
    {
        if (!reader_ || token.is_cancelled())
            co_return std::nullopt;
        co_return co_await reader_(token);
    }

    [[nodiscard]] auto content_length() const noexcept
        -> std::optional<std::uint64_t>
    {
        return content_length_;
    }

private:
    request_body_reader reader_;
    std::optional<std::uint64_t> content_length_;
};

/// Response bodies use the same pull/back-pressure contract as request bodies.
/// The alias keeps the implementation unified while making response APIs
/// self-documenting.
export using response_body_source = request_body_source;

// =============================================================================
// HTTP Method
// =============================================================================

export enum class http_method
{
    GET,
    HEAD,
    POST,
    PUT,
    DELETE_, // DELETE is a Windows macro
    CONNECT,
    OPTIONS,
    TRACE,
    PATCH,
};

export constexpr auto method_to_string(http_method m) noexcept
    -> std::string_view
{
    switch (m)
    {
    case http_method::GET:
        return "GET";
    case http_method::HEAD:
        return "HEAD";
    case http_method::POST:
        return "POST";
    case http_method::PUT:
        return "PUT";
    case http_method::DELETE_:
        return "DELETE";
    case http_method::CONNECT:
        return "CONNECT";
    case http_method::OPTIONS:
        return "OPTIONS";
    case http_method::TRACE:
        return "TRACE";
    case http_method::PATCH:
        return "PATCH";
    default:
        return "UNKNOWN";
    }
}

export constexpr auto string_to_method(std::string_view s) noexcept
    -> std::optional<http_method>
{
    if (s == "GET")
        return http_method::GET;
    if (s == "HEAD")
        return http_method::HEAD;
    if (s == "POST")
        return http_method::POST;
    if (s == "PUT")
        return http_method::PUT;
    if (s == "DELETE")
        return http_method::DELETE_;
    if (s == "CONNECT")
        return http_method::CONNECT;
    if (s == "OPTIONS")
        return http_method::OPTIONS;
    if (s == "TRACE")
        return http_method::TRACE;
    if (s == "PATCH")
        return http_method::PATCH;
    return std::nullopt;
}

// =============================================================================
// HTTP Version
// =============================================================================

export enum class http_version
{
    http_1_0,
    http_1_1,
    http_2, // HTTP/2 (RFC 9113)
    http_3, // HTTP/3 (RFC 9114) — reserved for future
};

export constexpr auto version_to_string(http_version v) noexcept
    -> std::string_view
{
    switch (v)
    {
    case http_version::http_1_0:
        return "HTTP/1.0";
    case http_version::http_1_1:
        return "HTTP/1.1";
    case http_version::http_2:
        return "HTTP/2";
    case http_version::http_3:
        return "HTTP/3";
    default:
        return "HTTP/1.1";
    }
}

export constexpr auto string_to_version(std::string_view s) noexcept
    -> std::optional<http_version>
{
    if (s == "HTTP/1.0")
        return http_version::http_1_0;
    if (s == "HTTP/1.1")
        return http_version::http_1_1;
    if (s == "HTTP/2" || s == "h2")
        return http_version::http_2;
    if (s == "HTTP/3" || s == "h3")
        return http_version::http_3;
    return std::nullopt;
}

// =============================================================================
// HTTP Status Code
// =============================================================================

export namespace status {
    // 1xx
    inline constexpr int continue_ = 100;
    inline constexpr int switching_protocols = 101;
    inline constexpr int processing = 102;
    inline constexpr int early_hints = 103;

    // 2xx
    inline constexpr int ok = 200;
    inline constexpr int created = 201;
    inline constexpr int accepted = 202;
    inline constexpr int non_authoritative_information = 203;
    inline constexpr int no_content = 204;
    inline constexpr int reset_content = 205;
    inline constexpr int partial_content = 206;
    inline constexpr int multi_status = 207;
    inline constexpr int already_reported = 208;
    inline constexpr int im_used = 226;

    // 3xx
    inline constexpr int multiple_choices = 300;
    inline constexpr int moved_permanently = 301;
    inline constexpr int found = 302;
    inline constexpr int see_other = 303;
    inline constexpr int not_modified = 304;
    inline constexpr int use_proxy = 305;
    inline constexpr int unused = 306;
    inline constexpr int temporary_redirect = 307;
    inline constexpr int permanent_redirect = 308;

    // 4xx
    inline constexpr int bad_request = 400;
    inline constexpr int unauthorized = 401;
    inline constexpr int payment_required = 402;
    inline constexpr int forbidden = 403;
    inline constexpr int not_found = 404;
    inline constexpr int method_not_allowed = 405;
    inline constexpr int not_acceptable = 406;
    inline constexpr int proxy_authentication_required = 407;
    inline constexpr int request_timeout = 408;
    inline constexpr int conflict = 409;
    inline constexpr int gone = 410;
    inline constexpr int length_required = 411;
    inline constexpr int precondition_failed = 412;
    inline constexpr int payload_too_large = 413;
    inline constexpr int uri_too_long = 414;
    inline constexpr int unsupported_media_type = 415;
    inline constexpr int range_not_satisfiable = 416;
    inline constexpr int expectation_failed = 417;
    inline constexpr int im_a_teapot = 418;
    inline constexpr int misdirected_request = 421;
    inline constexpr int unprocessable_content = 422;
    inline constexpr int locked = 423;
    inline constexpr int failed_dependency = 424;
    inline constexpr int too_early = 425;
    inline constexpr int upgrade_required = 426;
    inline constexpr int precondition_required = 428;
    inline constexpr int too_many_requests = 429;
    inline constexpr int request_header_fields_too_large = 431;
    inline constexpr int unavailable_for_legal_reasons = 451;

    // 5xx
    inline constexpr int internal_server_error = 500;
    inline constexpr int not_implemented = 501;
    inline constexpr int bad_gateway = 502;
    inline constexpr int service_unavailable = 503;
    inline constexpr int gateway_timeout = 504;
    inline constexpr int http_version_not_supported = 505;
    inline constexpr int variant_also_negotiates = 506;
    inline constexpr int insufficient_storage = 507;
    inline constexpr int loop_detected = 508;
    inline constexpr int not_extended = 510;
    inline constexpr int network_authentication_required = 511;
} // namespace status

export constexpr auto status_reason(int code) noexcept -> std::string_view
{
    switch (code)
    {
    case 100:
        return "Continue";
    case 101:
        return "Switching Protocols";
    case 102:
        return "Processing";
    case 103:
        return "Early Hints";
    case 200:
        return "OK";
    case 201:
        return "Created";
    case 202:
        return "Accepted";
    case 203:
        return "Non-Authoritative Information";
    case 204:
        return "No Content";
    case 205:
        return "Reset Content";
    case 206:
        return "Partial Content";
    case 207:
        return "Multi-Status";
    case 208:
        return "Already Reported";
    case 226:
        return "IM Used";
    case 300:
        return "Multiple Choices";
    case 301:
        return "Moved Permanently";
    case 302:
        return "Found";
    case 303:
        return "See Other";
    case 304:
        return "Not Modified";
    case 305:
        return "Use Proxy";
    case 306:
        return "(Unused)";
    case 307:
        return "Temporary Redirect";
    case 308:
        return "Permanent Redirect";
    case 400:
        return "Bad Request";
    case 401:
        return "Unauthorized";
    case 402:
        return "Payment Required";
    case 403:
        return "Forbidden";
    case 404:
        return "Not Found";
    case 405:
        return "Method Not Allowed";
    case 406:
        return "Not Acceptable";
    case 407:
        return "Proxy Authentication Required";
    case 408:
        return "Request Timeout";
    case 409:
        return "Conflict";
    case 410:
        return "Gone";
    case 411:
        return "Length Required";
    case 412:
        return "Precondition Failed";
    case 413:
        return "Payload Too Large";
    case 414:
        return "URI Too Long";
    case 415:
        return "Unsupported Media Type";
    case 416:
        return "Range Not Satisfiable";
    case 417:
        return "Expectation Failed";
    case 418:
        return "I'm a teapot";
    case 421:
        return "Misdirected Request";
    case 422:
        return "Unprocessable Content";
    case 423:
        return "Locked";
    case 424:
        return "Failed Dependency";
    case 425:
        return "Too Early";
    case 426:
        return "Upgrade Required";
    case 428:
        return "Precondition Required";
    case 429:
        return "Too Many Requests";
    case 431:
        return "Request Header Fields Too Large";
    case 451:
        return "Unavailable For Legal Reasons";
    case 500:
        return "Internal Server Error";
    case 501:
        return "Not Implemented";
    case 502:
        return "Bad Gateway";
    case 503:
        return "Service Unavailable";
    case 504:
        return "Gateway Timeout";
    case 505:
        return "HTTP Version Not Supported";
    case 506:
        return "Variant Also Negotiates";
    case 507:
        return "Insufficient Storage";
    case 508:
        return "Loop Detected";
    case 510:
        return "Not Extended";
    case 511:
        return "Network Authentication Required";
    default:
        return "Unknown";
    }
}

// =============================================================================
// Case-Insensitive Header Map
// =============================================================================

export struct case_insensitive_less
{
    auto operator()(std::string_view a, std::string_view b) const noexcept
        -> bool;
};

export using header_map =
    cnetmod::flat_map<std::string, std::string, case_insensitive_less>;

// =============================================================================
// Simple URL Structure
// =============================================================================

export struct url
{
    std::string scheme; // "http", "https", "ws", "wss"
    std::string host;
    std::uint16_t port = 0;
    std::string path;     // With leading '/'
    std::string query;    // Without '?'
    std::string fragment; // Without '#'

    /// Parse URL string
    [[nodiscard]] static auto parse(std::string_view input)
        -> std::expected<url, std::string>;
};

export auto format_authority_host(std::string_view host) -> std::string;

export auto format_authority(std::string_view host, std::uint16_t port,
    bool use_ssl) -> std::string;

// =============================================================================
// HTTP Limit Constants
// =============================================================================

export inline constexpr std::size_t max_header_size = 16 * 1024;      // 16 KB
export inline constexpr std::size_t max_body_size = 32 * 1024 * 1024; // 32 MB

// =============================================================================
// HTTP Error Codes
// =============================================================================

export enum class http_errc
{
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
    auto http_category_instance() -> const std::error_category&;
}

export auto make_error_code(http_errc e) noexcept -> std::error_code;

} // namespace cnetmod::http

template <>
struct std::is_error_code_enum<cnetmod::http::http_errc> : std::true_type
{
};
