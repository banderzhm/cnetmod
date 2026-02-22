module;

#include <cnetmod/config.hpp>
#include <cstring>

export module cnetmod.protocol.http:parser;

import std;
import :types;

namespace cnetmod::http {

// =============================================================================
// Helper: Find \r\n
// =============================================================================

namespace detail {

/// Find "\r\n" in [begin, end), return position of '\r', return npos if not found
inline auto find_crlf(const char* data, std::size_t size) noexcept
    -> std::size_t
{
    for (std::size_t i = 0; i + 1 < size; ++i) {
        if (data[i] == '\r' && data[i + 1] == '\n')
            return i;
    }
    return std::string_view::npos;
}

/// Trim leading and trailing whitespace
inline auto trim(std::string_view s) noexcept -> std::string_view {
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
        s.remove_suffix(1);
    return s;
}

} // namespace detail

// =============================================================================
// request_parser — Streaming HTTP Request Parser
// =============================================================================

export class request_parser {
public:
    request_parser() = default;

    /// Parse input data, returns number of bytes consumed
    /// If parsing is complete, ready() returns true
    /// Returns error_code on error
    [[nodiscard]] auto consume(const char* data, std::size_t len)
        -> std::expected<std::size_t, std::error_code>
    {
        if (ready_) return std::size_t{0};

        buf_.append(data, len);
        std::size_t total_consumed = len;

        while (!ready_) {
            switch (state_) {
            case state::request_line: {
                auto pos = detail::find_crlf(buf_.data(), buf_.size());
                if (pos == std::string_view::npos) {
                    if (buf_.size() > max_header_size)
                        return std::unexpected(make_error_code(http_errc::header_too_large));
                    return total_consumed;
                }

                auto line = std::string_view(buf_.data(), pos);
                auto r = parse_request_line(line);
                if (!r) return std::unexpected(r.error());

                header_bytes_ += pos + 2;
                buf_.erase(0, pos + 2);
                state_ = state::headers;
                break;
            }
            case state::headers: {
                auto pos = detail::find_crlf(buf_.data(), buf_.size());
                if (pos == std::string_view::npos) {
                    if (header_bytes_ + buf_.size() > max_header_size)
                        return std::unexpected(make_error_code(http_errc::header_too_large));
                    return total_consumed;
                }

                if (pos == 0) {
                    // Empty line: headers end
                    buf_.erase(0, 2);
                    header_bytes_ += 2;

                    if (!prepare_body()) {
                        ready_ = true;
                    } else {
                        state_ = state::body;
                    }
                    break;
                }

                auto line = std::string_view(buf_.data(), pos);
                auto r = parse_header_line(line);
                if (!r) return std::unexpected(r.error());

                header_bytes_ += pos + 2;
                buf_.erase(0, pos + 2);
                break;
            }
            case state::body: {
                if (chunked_) {
                    auto r = process_chunked_body();
                    if (!r) return std::unexpected(r.error());
                    if (!*r) return total_consumed; // Need more data
                } else {
                    auto available = buf_.size();
                    auto need = body_bytes_remaining_;
                    auto take = std::min(available, need);
                    body_.append(buf_.data(), take);
                    buf_.erase(0, take);
                    body_bytes_remaining_ -= take;

                    if (body_bytes_remaining_ > 0)
                        return total_consumed;
                }
                ready_ = true;
                break;
            }
            } // switch
        }

        return total_consumed;
    }

    /// Check if parsing is complete
    [[nodiscard]] auto ready() const noexcept -> bool { return ready_; }

    // Access parsing results
    [[nodiscard]] auto method() const noexcept -> std::string_view { return method_; }
    [[nodiscard]] auto method_enum() const noexcept -> std::optional<http_method> {
        return string_to_method(method_);
    }
    [[nodiscard]] auto uri() const noexcept -> std::string_view { return uri_; }
    [[nodiscard]] auto version() const noexcept -> http_version { return version_; }
    [[nodiscard]] auto headers() const noexcept -> const header_map& { return headers_; }
    [[nodiscard]] auto body() const noexcept -> std::string_view { return body_; }

    [[nodiscard]] auto get_header(std::string_view key) const -> std::string_view {
        auto it = headers_.find(std::string(key));
        if (it != headers_.end()) return it->second;
        return {};
    }

    /// Reset parser for reuse
    void reset() noexcept {
        buf_.clear();
        method_.clear();
        uri_.clear();
        version_ = http_version::http_1_1;
        headers_.clear();
        body_.clear();
        state_ = state::request_line;
        header_bytes_ = 0;
        body_bytes_remaining_ = 0;
        chunked_ = false;
        ready_ = false;
    }

private:
    enum class state { request_line, headers, body };

    auto parse_request_line(std::string_view line)
        -> std::expected<void, std::error_code>
    {
        // "GET /path HTTP/1.1"
        auto sp1 = line.find(' ');
        if (sp1 == std::string_view::npos)
            return std::unexpected(make_error_code(http_errc::invalid_method));

        method_ = std::string(line.substr(0, sp1));
        auto rest = line.substr(sp1 + 1);

        auto sp2 = rest.find(' ');
        if (sp2 == std::string_view::npos)
            return std::unexpected(make_error_code(http_errc::invalid_uri));

        uri_ = std::string(rest.substr(0, sp2));
        auto ver = rest.substr(sp2 + 1);

        auto v = string_to_version(ver);
        if (!v) return std::unexpected(make_error_code(http_errc::invalid_version));
        version_ = *v;

        return {};
    }

    auto parse_header_line(std::string_view line)
        -> std::expected<void, std::error_code>
    {
        auto colon = line.find(':');
        if (colon == std::string_view::npos)
            return std::unexpected(make_error_code(http_errc::invalid_header));

        auto key = detail::trim(line.substr(0, colon));
        auto val = detail::trim(line.substr(colon + 1));

        if (key.empty())
            return std::unexpected(make_error_code(http_errc::invalid_header));

        auto it = headers_.find(std::string(key));
        if (it != headers_.end()) {
            // Append multi-value header (e.g., Set-Cookie)
            it->second += ", ";
            it->second += val;
        } else {
            headers_.emplace(std::string(key), std::string(val));
        }
        return {};
    }

    /// Check if body exists, set body reading mode
    auto prepare_body() -> bool {
        // chunked?
        auto te = get_header("Transfer-Encoding");
        if (te.find("chunked") != std::string_view::npos) {
            chunked_ = true;
            return true;
        }
        // Content-Length?
        auto cl = get_header("Content-Length");
        if (!cl.empty()) {
            std::size_t len = 0;
            auto [ptr, ec] = std::from_chars(cl.data(), cl.data() + cl.size(), len);
            if (ec == std::errc{} && len > 0) {
                if (len > max_body_size) {
                    body_bytes_remaining_ = 0;
                    return false; // Will error in subsequent processing
                }
                body_bytes_remaining_ = len;
                body_.reserve(len);
                return true;
            }
        }
        return false;
    }

    /// Process chunked body, returns true when body is complete
    auto process_chunked_body()
        -> std::expected<bool, std::error_code>
    {
        for (;;) {
            auto pos = detail::find_crlf(buf_.data(), buf_.size());
            if (pos == std::string_view::npos)
                return false; // Need more data

            auto size_str = std::string_view(buf_.data(), pos);
            // Parse chunk size (hexadecimal)
            std::size_t chunk_size = 0;
            auto [ptr, ec] = std::from_chars(
                size_str.data(), size_str.data() + size_str.size(),
                chunk_size, 16);
            if (ec != std::errc{})
                return std::unexpected(make_error_code(http_errc::invalid_chunk));

            if (chunk_size == 0) {
                // Last chunk, skip "0\r\n\r\n"
                buf_.erase(0, pos + 2);
                // Skip trailing \r\n
                if (buf_.size() >= 2 && buf_[0] == '\r' && buf_[1] == '\n')
                    buf_.erase(0, 2);
                return true;
            }

            // Need chunk_size + \r\n (the \r\n after chunk data)
            auto data_start = pos + 2;
            if (buf_.size() < data_start + chunk_size + 2)
                return false; // Need more data

            body_.append(buf_.data() + data_start, chunk_size);
            buf_.erase(0, data_start + chunk_size + 2);

            if (body_.size() > max_body_size)
                return std::unexpected(make_error_code(http_errc::body_too_large));
        }
    }

    std::string buf_;
    std::string method_;
    std::string uri_;
    http_version version_ = http_version::http_1_1;
    header_map headers_;
    std::string body_;

    state state_ = state::request_line;
    std::size_t header_bytes_ = 0;
    std::size_t body_bytes_remaining_ = 0;
    bool chunked_ = false;
    bool ready_ = false;
};

// =============================================================================
// response_parser — Streaming HTTP Response Parser
// =============================================================================

export class response_parser {
public:
    response_parser() = default;

    /// Parse input data, returns number of bytes consumed
    [[nodiscard]] auto consume(const char* data, std::size_t len)
        -> std::expected<std::size_t, std::error_code>
    {
        if (ready_) return std::size_t{0};

        buf_.append(data, len);
        std::size_t total_consumed = len;

        while (!ready_) {
            switch (state_) {
            case state::status_line: {
                auto pos = detail::find_crlf(buf_.data(), buf_.size());
                if (pos == std::string_view::npos) {
                    if (buf_.size() > max_header_size)
                        return std::unexpected(make_error_code(http_errc::header_too_large));
                    return total_consumed;
                }

                auto line = std::string_view(buf_.data(), pos);
                auto r = parse_status_line(line);
                if (!r) return std::unexpected(r.error());

                header_bytes_ += pos + 2;
                buf_.erase(0, pos + 2);
                state_ = state::headers;
                break;
            }
            case state::headers: {
                auto pos = detail::find_crlf(buf_.data(), buf_.size());
                if (pos == std::string_view::npos) {
                    if (header_bytes_ + buf_.size() > max_header_size)
                        return std::unexpected(make_error_code(http_errc::header_too_large));
                    return total_consumed;
                }

                if (pos == 0) {
                    buf_.erase(0, 2);
                    header_bytes_ += 2;

                    if (!prepare_body()) {
                        ready_ = true;
                    } else {
                        state_ = state::body;
                    }
                    break;
                }

                auto line = std::string_view(buf_.data(), pos);
                auto r = parse_header_line(line);
                if (!r) return std::unexpected(r.error());

                header_bytes_ += pos + 2;
                buf_.erase(0, pos + 2);
                break;
            }
            case state::body: {
                if (chunked_) {
                    auto r = process_chunked_body();
                    if (!r) return std::unexpected(r.error());
                    if (!*r) return total_consumed;
                } else {
                    auto available = buf_.size();
                    auto need = body_bytes_remaining_;
                    auto take = std::min(available, need);
                    body_.append(buf_.data(), take);
                    buf_.erase(0, take);
                    body_bytes_remaining_ -= take;

                    if (body_bytes_remaining_ > 0)
                        return total_consumed;
                }
                ready_ = true;
                break;
            }
            }
        }

        return total_consumed;
    }

    [[nodiscard]] auto ready() const noexcept -> bool { return ready_; }

    // Access parsing results
    [[nodiscard]] auto version() const noexcept -> http_version { return version_; }
    [[nodiscard]] auto status_code() const noexcept -> int { return status_code_; }
    [[nodiscard]] auto status_message() const noexcept -> std::string_view { return status_msg_; }
    [[nodiscard]] auto headers() const noexcept -> const header_map& { return headers_; }
    [[nodiscard]] auto body() const noexcept -> std::string_view { return body_; }

    [[nodiscard]] auto get_header(std::string_view key) const -> std::string_view {
        auto it = headers_.find(std::string(key));
        if (it != headers_.end()) return it->second;
        return {};
    }

    void reset() noexcept {
        buf_.clear();
        version_ = http_version::http_1_1;
        status_code_ = 0;
        status_msg_.clear();
        headers_.clear();
        body_.clear();
        state_ = state::status_line;
        header_bytes_ = 0;
        body_bytes_remaining_ = 0;
        chunked_ = false;
        ready_ = false;
    }

private:
    enum class state { status_line, headers, body };

    auto parse_status_line(std::string_view line)
        -> std::expected<void, std::error_code>
    {
        // "HTTP/1.1 200 OK"
        auto sp1 = line.find(' ');
        if (sp1 == std::string_view::npos)
            return std::unexpected(make_error_code(http_errc::invalid_status_line));

        auto ver = line.substr(0, sp1);
        auto v = string_to_version(ver);
        if (!v) return std::unexpected(make_error_code(http_errc::invalid_version));
        version_ = *v;

        auto rest = line.substr(sp1 + 1);
        auto sp2 = rest.find(' ');

        std::string_view code_str;
        if (sp2 != std::string_view::npos) {
            code_str = rest.substr(0, sp2);
            status_msg_ = std::string(rest.substr(sp2 + 1));
        } else {
            code_str = rest;
        }

        auto [ptr, ec] = std::from_chars(code_str.data(), code_str.data() + code_str.size(), status_code_);
        if (ec != std::errc{})
            return std::unexpected(make_error_code(http_errc::invalid_status_line));

        return {};
    }

    auto parse_header_line(std::string_view line)
        -> std::expected<void, std::error_code>
    {
        auto colon = line.find(':');
        if (colon == std::string_view::npos)
            return std::unexpected(make_error_code(http_errc::invalid_header));

        auto key = detail::trim(line.substr(0, colon));
        auto val = detail::trim(line.substr(colon + 1));

        if (key.empty())
            return std::unexpected(make_error_code(http_errc::invalid_header));

        auto it = headers_.find(std::string(key));
        if (it != headers_.end()) {
            it->second += ", ";
            it->second += val;
        } else {
            headers_.emplace(std::string(key), std::string(val));
        }
        return {};
    }

    auto prepare_body() -> bool {
        auto te = get_header("Transfer-Encoding");
        if (te.find("chunked") != std::string_view::npos) {
            chunked_ = true;
            return true;
        }
        auto cl = get_header("Content-Length");
        if (!cl.empty()) {
            std::size_t len = 0;
            auto [ptr, ec] = std::from_chars(cl.data(), cl.data() + cl.size(), len);
            if (ec == std::errc{} && len > 0) {
                if (len > max_body_size) {
                    body_bytes_remaining_ = 0;
                    return false;
                }
                body_bytes_remaining_ = len;
                body_.reserve(len);
                return true;
            }
        }
        return false;
    }

    auto process_chunked_body()
        -> std::expected<bool, std::error_code>
    {
        for (;;) {
            auto pos = detail::find_crlf(buf_.data(), buf_.size());
            if (pos == std::string_view::npos)
                return false;

            auto size_str = std::string_view(buf_.data(), pos);
            std::size_t chunk_size = 0;
            auto [ptr, ec] = std::from_chars(
                size_str.data(), size_str.data() + size_str.size(),
                chunk_size, 16);
            if (ec != std::errc{})
                return std::unexpected(make_error_code(http_errc::invalid_chunk));

            if (chunk_size == 0) {
                buf_.erase(0, pos + 2);
                if (buf_.size() >= 2 && buf_[0] == '\r' && buf_[1] == '\n')
                    buf_.erase(0, 2);
                return true;
            }

            auto data_start = pos + 2;
            if (buf_.size() < data_start + chunk_size + 2)
                return false;

            body_.append(buf_.data() + data_start, chunk_size);
            buf_.erase(0, data_start + chunk_size + 2);

            if (body_.size() > max_body_size)
                return std::unexpected(make_error_code(http_errc::body_too_large));
        }
    }

    std::string buf_;
    http_version version_ = http_version::http_1_1;
    int status_code_ = 0;
    std::string status_msg_;
    header_map headers_;
    std::string body_;

    state state_ = state::status_line;
    std::size_t header_bytes_ = 0;
    std::size_t body_bytes_remaining_ = 0;
    bool chunked_ = false;
    bool ready_ = false;
};

} // namespace cnetmod::http
