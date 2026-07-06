module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.grpc.types;

import std;
import cnetmod.protocol.http;

namespace cnetmod::grpc {

export enum class status_code : int {
    ok = 0,
    cancelled = 1,
    unknown = 2,
    invalid_argument = 3,
    deadline_exceeded = 4,
    not_found = 5,
    already_exists = 6,
    permission_denied = 7,
    resource_exhausted = 8,
    failed_precondition = 9,
    aborted = 10,
    out_of_range = 11,
    unimplemented = 12,
    internal = 13,
    unavailable = 14,
    data_loss = 15,
    unauthenticated = 16,
};

export using byte_buffer = std::vector<std::byte>;
export using metadata = std::multimap<std::string, std::string>;

export inline constexpr std::size_t default_max_message_bytes = 4u * 1024u * 1024u;
export inline constexpr std::size_t default_max_metadata_bytes = 8u * 1024u;

export enum class compression_algorithm {
    identity,
    gzip,
};

export struct status {
    status_code code = status_code::ok;
    std::string message;
    metadata trailers;

    [[nodiscard]] auto ok() const noexcept -> bool {
        return code == status_code::ok;
    }
};

export struct message_frame {
    bool compressed = false;
    byte_buffer payload;
};

export struct call_options {
    metadata headers;
    std::chrono::milliseconds timeout{0};
    compression_algorithm compression = compression_algorithm::identity;
};

export struct call_context {
    std::string service;
    std::string method;
    std::string path;
    metadata headers;
    std::chrono::milliseconds timeout{0};
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();

    [[nodiscard]] auto deadline_exceeded() const noexcept -> bool {
        return timeout.count() > 0 &&
               std::chrono::steady_clock::now() - started > timeout;
    }
};

export struct unary_request {
    std::string service;
    std::string method;
    metadata headers;
    byte_buffer payload;
    std::chrono::milliseconds timeout{0};
    compression_algorithm compression = compression_algorithm::identity;
};

export struct unary_response {
    status st;
    metadata headers;
    byte_buffer payload;
};

export struct streaming_request {
    std::string service;
    std::string method;
    metadata headers;
    std::vector<byte_buffer> messages;
    std::chrono::milliseconds timeout{0};
    compression_algorithm compression = compression_algorithm::identity;
};

export struct streaming_response {
    status st;
    metadata headers;
    std::vector<byte_buffer> messages;
};

export enum class call_kind {
    unary,
    client_streaming,
    server_streaming,
    bidi_streaming,
};

namespace detail {

inline auto lower(std::string_view text) -> std::string {
    std::string out(text);
    std::ranges::transform(out, out.begin(),
        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

inline auto percent_decode(std::string_view text) -> std::string {
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size()) {
            unsigned value = 0;
            auto hex = text.substr(i + 1, 2);
            auto [ptr, ec] = std::from_chars(hex.data(), hex.data() + hex.size(), value, 16);
            if (ec == std::errc{}) {
                out.push_back(static_cast<char>(value));
                i += 2;
                continue;
            }
        }
        out.push_back(text[i]);
    }
    return out;
}

inline auto percent_encode(std::string_view text) -> std::string {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char ch : text) {
        if (ch >= 0x20 && ch < 0x7f && ch != '%') {
            out.push_back(static_cast<char>(ch));
        } else {
            out.push_back('%');
            out.push_back(hex[(ch >> 4) & 0xf]);
            out.push_back(hex[ch & 0xf]);
        }
    }
    return out;
}

inline auto header_value(const http::header_map& headers, std::string_view name)
    -> std::string_view
{
    auto it = headers.find(std::string(name));
    if (it != headers.end()) return it->second;
    auto lname = lower(name);
    it = headers.find(lname);
    if (it != headers.end()) return it->second;
    for (const auto& [k, v] : headers) {
        if (lower(k) == lname) return v;
    }
    return {};
}

inline auto metadata_value(const metadata& md, std::string_view name) -> std::string_view {
    auto lname = lower(name);
    for (const auto& [k, v] : md) {
        if (lower(k) == lname) return v;
    }
    return {};
}

inline auto base64_table() -> std::string_view {
    return "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
}

inline auto base64_encode(std::span<const std::byte> data) -> std::string {
    auto table = base64_table();
    std::string out;
    out.reserve(((data.size() + 2) / 3) * 4);
    for (std::size_t i = 0; i < data.size(); i += 3) {
        std::uint32_t n = static_cast<std::uint32_t>(std::to_integer<unsigned char>(data[i])) << 16;
        if (i + 1 < data.size()) n |= static_cast<std::uint32_t>(std::to_integer<unsigned char>(data[i + 1])) << 8;
        if (i + 2 < data.size()) n |= static_cast<std::uint32_t>(std::to_integer<unsigned char>(data[i + 2]));
        out.push_back(table[(n >> 18) & 0x3f]);
        out.push_back(table[(n >> 12) & 0x3f]);
        out.push_back(i + 1 < data.size() ? table[(n >> 6) & 0x3f] : '=');
        out.push_back(i + 2 < data.size() ? table[n & 0x3f] : '=');
    }
    return out;
}

inline auto base64_decode(std::string_view text) -> std::optional<byte_buffer> {
    auto decode = [](char ch) -> int {
        if (ch >= 'A' && ch <= 'Z') return ch - 'A';
        if (ch >= 'a' && ch <= 'z') return ch - 'a' + 26;
        if (ch >= '0' && ch <= '9') return ch - '0' + 52;
        if (ch == '+') return 62;
        if (ch == '/') return 63;
        if (ch == '=') return -2;
        return -1;
    };

    byte_buffer out;
    std::array<int, 4> q{};
    std::size_t qi = 0;
    for (char ch : text) {
        if (std::isspace(static_cast<unsigned char>(ch))) continue;
        auto v = decode(ch);
        if (v == -1) return std::nullopt;
        q[qi++] = v;
        if (qi != 4) continue;

        if (q[0] < 0 || q[1] < 0) return std::nullopt;
        std::uint32_t n = (static_cast<std::uint32_t>(q[0]) << 18) |
                          (static_cast<std::uint32_t>(q[1]) << 12);
        if (q[2] >= 0) n |= static_cast<std::uint32_t>(q[2]) << 6;
        if (q[3] >= 0) n |= static_cast<std::uint32_t>(q[3]);
        out.push_back(static_cast<std::byte>((n >> 16) & 0xff));
        if (q[2] != -2) out.push_back(static_cast<std::byte>((n >> 8) & 0xff));
        if (q[3] != -2) out.push_back(static_cast<std::byte>(n & 0xff));
        qi = 0;
    }
    if (qi != 0) return std::nullopt;
    return out;
}

inline auto parse_timeout(std::string_view text) -> std::optional<std::chrono::nanoseconds> {
    if (text.size() < 2 || text.size() > 9) return std::nullopt;
    auto unit = text.back();
    auto value_text = text.substr(0, text.size() - 1);
    std::uint64_t value = 0;
    auto [ptr, ec] = std::from_chars(value_text.data(), value_text.data() + value_text.size(), value);
    if (ec != std::errc{} || ptr != value_text.data() + value_text.size()) return std::nullopt;

    switch (unit) {
    case 'H': return std::chrono::hours(value);
    case 'M': return std::chrono::minutes(value);
    case 'S': return std::chrono::seconds(value);
    case 'm': return std::chrono::milliseconds(value);
    case 'u': return std::chrono::microseconds(value);
    case 'n': return std::chrono::nanoseconds(value);
    default: return std::nullopt;
    }
}

inline auto format_timeout(std::chrono::milliseconds timeout) -> std::string {
    if (timeout.count() <= 0) return {};
    auto count = timeout.count();
    if (count <= 99'999'999) return std::to_string(count) + "m";
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(timeout).count();
    if (seconds <= 99'999'999) return std::to_string(seconds) + "S";
    auto minutes = std::chrono::duration_cast<std::chrono::minutes>(timeout).count();
    if (minutes <= 99'999'999) return std::to_string(minutes) + "M";
    auto hours = std::chrono::duration_cast<std::chrono::hours>(timeout).count();
    return std::to_string(std::min<std::int64_t>(hours, 99'999'999)) + "H";
}

} // namespace detail

export auto service_path(std::string_view service, std::string_view method)
    -> std::string
{
    std::string path = "/";
    path += service;
    path += "/";
    path += method;
    return path;
}

export auto make_status(status_code code, std::string message = {},
                        metadata trailers = {}) -> status
{
    return status{
        .code = code,
        .message = std::move(message),
        .trailers = std::move(trailers),
    };
}

export auto compression_name(compression_algorithm algorithm) -> std::string_view {
    switch (algorithm) {
    case compression_algorithm::identity: return "identity";
    case compression_algorithm::gzip: return "gzip";
    }
    return "identity";
}

export auto compression_from_header(std::string_view text) -> std::optional<compression_algorithm> {
    auto lowered = detail::lower(text);
    if (lowered.empty() || lowered == "identity") return compression_algorithm::identity;
    if (lowered == "gzip") return compression_algorithm::gzip;
    return std::nullopt;
}

export auto accepts_compression(std::string_view header, compression_algorithm algorithm) -> bool {
    if (algorithm == compression_algorithm::identity) return true;
    auto wanted = compression_name(algorithm);
    std::size_t start = 0;
    while (start <= header.size()) {
        auto comma = header.find(',', start);
        auto token = header.substr(start, comma == std::string_view::npos ? header.size() - start : comma - start);
        while (!token.empty() && std::isspace(static_cast<unsigned char>(token.front()))) token.remove_prefix(1);
        while (!token.empty() && std::isspace(static_cast<unsigned char>(token.back()))) token.remove_suffix(1);
        if (detail::lower(token) == wanted) return true;
        if (comma == std::string_view::npos) break;
        start = comma + 1;
    }
    return false;
}

export auto parse_service_path(std::string_view path)
    -> std::optional<std::pair<std::string, std::string>>
{
    if (path.empty() || path.front() != '/') return std::nullopt;
    path.remove_prefix(1);
    auto slash = path.rfind('/');
    if (slash == std::string_view::npos || slash == 0 || slash + 1 >= path.size()) {
        return std::nullopt;
    }
    return std::pair{std::string(path.substr(0, slash)), std::string(path.substr(slash + 1))};
}

export auto metadata_from_headers(const http::header_map& headers) -> metadata {
    metadata md;
    for (const auto& [k, v] : headers) {
        auto key = detail::lower(k);
        if (key.starts_with(":")) continue;
        if (key == "content-type" || key == "content-length" || key == "te" ||
            key == "grpc-status" || key == "grpc-message") {
            continue;
        }
        md.emplace(std::move(key), v);
    }
    return md;
}

export auto header_value(const http::header_map& headers, std::string_view name)
    -> std::string_view
{
    return detail::header_value(headers, name);
}

export auto metadata_value(const metadata& md, std::string_view name)
    -> std::string_view
{
    return detail::metadata_value(md, name);
}

export auto metadata_wire_size(const metadata& md) noexcept -> std::size_t {
    std::size_t total = 0;
    for (const auto& [k, v] : md) {
        total += k.size() + v.size() + 4;
    }
    return total;
}

export void append_metadata_headers(http::request& req, const metadata& md) {
    for (const auto& [k, v] : md) {
        req.append_header(k, v);
    }
}

export void append_metadata_headers(http::response& resp, const metadata& md) {
    for (const auto& [k, v] : md) {
        resp.append_header(k, v);
    }
}

export void append_metadata_trailers(http::response& resp, const metadata& md) {
    for (const auto& [k, v] : md) {
        resp.append_trailer(k, v);
    }
}

export auto status_from_headers(const http::header_map& headers) -> status {
    status st;
    auto code_text = detail::header_value(headers, "grpc-status");
    if (code_text.empty()) {
        st.code = status_code::unknown;
        st.message = "missing grpc-status";
        return st;
    }
    int code = 0;
    auto [ptr, ec] = std::from_chars(code_text.data(), code_text.data() + code_text.size(), code);
    if (ec != std::errc{}) {
        st.code = status_code::unknown;
        st.message = "invalid grpc-status";
        return st;
    }
    st.code = static_cast<status_code>(code);
    st.message = detail::percent_decode(detail::header_value(headers, "grpc-message"));
    return st;
}

export auto status_from_response(const http::response& resp) -> status {
    if (!detail::header_value(resp.trailers(), "grpc-status").empty()) {
        return status_from_headers(resp.trailers());
    }
    return status_from_headers(resp.headers());
}

export auto timeout_from_headers(const http::header_map& headers)
    -> std::chrono::milliseconds
{
    auto text = detail::header_value(headers, "grpc-timeout");
    if (text.empty()) return std::chrono::milliseconds{0};
    auto ns = detail::parse_timeout(text);
    if (!ns) return std::chrono::milliseconds{0};
    return std::chrono::duration_cast<std::chrono::milliseconds>(*ns);
}

export auto format_timeout(std::chrono::milliseconds timeout) -> std::string {
    return detail::format_timeout(timeout);
}

export void add_binary_metadata(metadata& md, std::string key, std::span<const std::byte> value) {
    if (!key.ends_with("-bin")) key += "-bin";
    md.emplace(std::move(key), detail::base64_encode(value));
}

export auto get_binary_metadata(const metadata& md, std::string_view key)
    -> std::vector<byte_buffer>
{
    std::string k(key);
    if (!k.ends_with("-bin")) k += "-bin";
    std::vector<byte_buffer> out;
    auto range = md.equal_range(k);
    for (auto it = range.first; it != range.second; ++it) {
        auto decoded = detail::base64_decode(it->second);
        if (decoded) out.push_back(std::move(*decoded));
    }
    return out;
}

} // namespace cnetmod::grpc
