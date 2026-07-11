module cnetmod.protocol.http;

import std;
import :parser;
import :semantics;

namespace cnetmod::http
{
    namespace detail
    {
        auto find_crlf(const char* data, std::size_t size) noexcept -> std::size_t
        {
            for (std::size_t i = 0; i + 1 < size; ++i)if (data[i] == '\r' && data[i + 1] == '\n')return i;
            return std::string_view::npos;
        }

        auto trim(std::string_view value) noexcept -> std::string_view
        {
            while (!value.empty() && (value.front() == ' ' || value.front() == '\t'))value.remove_prefix(1);
            while (!value.empty() && (value.back() == ' ' || value.back() == '\t'))value.remove_suffix(1);
            return value;
        }
    }

    auto request_parser::ready() const noexcept -> bool { return ready_; }
    auto request_parser::method() const noexcept -> std::string_view { return method_; }

    auto request_parser::method_enum() const noexcept -> std::optional<http_method>
    {
        return string_to_method(method_);
    }

    auto request_parser::uri() const noexcept -> std::string_view { return uri_; }
    auto request_parser::version() const noexcept -> http_version { return version_; }
    auto request_parser::headers() const noexcept -> const header_map& { return headers_; }
    auto request_parser::body() const noexcept -> std::string_view { return body_; }

    auto request_parser::get_header(std::string_view k) const -> std::string_view
    {
        auto it = headers_.find(std::string(k));
        return it == headers_.end() ? std::string_view{} : std::string_view{it->second};
    }

    void request_parser::reset() noexcept
    {
        buf_.clear();
        method_.clear();
        uri_.clear();
        version_ = http_version::http_1_1;
        headers_.clear();
        body_.clear();
        state_ = state::request_line;
        header_bytes_ = body_bytes_remaining_ = 0;
        chunked_ = ready_ = false;
    }

    auto request_parser::parse_request_line(std::string_view l) -> std::expected<void, std::error_code>
    {
        auto a = l.find(' ');
        if (a == std::string_view::npos)return std::unexpected(make_error_code(http_errc::invalid_method));
        method_ = std::string(l.substr(0, a));
        auto r = l.substr(a + 1);
        auto b = r.find(' ');
        if (b == std::string_view::npos)return std::unexpected(make_error_code(http_errc::invalid_uri));
        uri_ = std::string(r.substr(0, b));
        auto v = string_to_version(r.substr(b + 1));
        if (!v)return std::unexpected(make_error_code(http_errc::invalid_version));
        version_ = *v;
        return {};
    }

    auto request_parser::parse_header_line(std::string_view l) -> std::expected<void, std::error_code>
    {
        auto c = l.find(':');
        if (c == std::string_view::npos)return std::unexpected(make_error_code(http_errc::invalid_header));
        auto k = detail::trim(l.substr(0, c));
        auto v = detail::trim(l.substr(c + 1));
        if (k.empty())return std::unexpected(make_error_code(http_errc::invalid_header));
        auto it = headers_.find(std::string(k));
        if (it == headers_.end())headers_.emplace(std::string(k), std::string(v));
        else
        {
            it->second += ", ";
            it->second += v;
        }
        return {};
    }

    auto request_parser::prepare_body() -> bool
    {
        auto t = get_header("Transfer-Encoding");
        if (t.find("chunked") != std::string_view::npos)
        {
            chunked_ = true;
            return true;
        }
        auto c = get_header("Content-Length");
        std::size_t n{};
        auto [p,e] = std::from_chars(c.data(), c.data() + c.size(), n);
        if (!c.empty() && e == std::errc{} && n)
        {
            if (n > max_body_size)return false;
            body_bytes_remaining_ = n;
            body_.reserve(n);
            return true;
        }
        return false;
    }

    auto request_parser::process_chunked_body() -> std::expected<bool, std::error_code>
    {
        for (;;)
        {
            auto p = detail::find_crlf(buf_.data(), buf_.size());
            if (p == std::string_view::npos)return false;
            std::size_t n{};
            auto [q,e] = std::from_chars(buf_.data(), buf_.data() + p, n, 16);
            if (e != std::errc{})return std::unexpected(make_error_code(http_errc::invalid_chunk));
            if (!n)
            {
                buf_.erase(0, p + 2);
                if (buf_.starts_with("\r\n"))buf_.erase(0, 2);
                return true;
            }
            auto start = p + 2;
            if (buf_.size() < start + n + 2)return false;
            body_.append(buf_.data() + start, n);
            buf_.erase(0, start + n + 2);
            if (body_.size() > max_body_size)return std::unexpected(make_error_code(http_errc::body_too_large));
        }
    }

    auto request_parser::consume(const char* data, std::size_t len)
        -> std::expected<std::size_t, std::error_code>
    {
        if (ready_) return std::size_t{0};
        buf_.append(data, len);
        const std::size_t total_consumed = len;
        while (!ready_)
        {
            switch (state_)
            {
            case state::request_line:
                {
                    const auto pos = detail::find_crlf(buf_.data(), buf_.size());
                    if (pos == std::string_view::npos)
                    {
                        if (buf_.size() > max_header_size)
                            return std::unexpected(make_error_code(http_errc::header_too_large));
                        return total_consumed;
                    }
                    auto parsed = parse_request_line({buf_.data(), pos});
                    if (!parsed) return std::unexpected(parsed.error());
                    header_bytes_ += pos + 2;
                    buf_.erase(0, pos + 2);
                    state_ = state::headers;
                    break;
                }
            case state::headers:
                {
                    const auto pos = detail::find_crlf(buf_.data(), buf_.size());
                    if (pos == std::string_view::npos)
                    {
                        if (header_bytes_ + buf_.size() > max_header_size)
                            return std::unexpected(make_error_code(http_errc::header_too_large));
                        return total_consumed;
                    }
                    if (pos == 0)
                    {
                        buf_.erase(0, 2);
                        header_bytes_ += 2;
                        if (!prepare_body()) ready_ = true;
                        else state_ = state::body;
                        break;
                    }
                    auto parsed = parse_header_line({buf_.data(), pos});
                    if (!parsed) return std::unexpected(parsed.error());
                    header_bytes_ += pos + 2;
                    buf_.erase(0, pos + 2);
                    break;
                }
            case state::body:
                {
                    if (chunked_)
                    {
                        auto parsed = process_chunked_body();
                        if (!parsed) return std::unexpected(parsed.error());
                        if (!*parsed) return total_consumed;
                    }
                    else
                    {
                        const auto take = std::min(buf_.size(), body_bytes_remaining_);
                        body_.append(buf_.data(), take);
                        buf_.erase(0, take);
                        body_bytes_remaining_ -= take;
                        if (body_bytes_remaining_ != 0) return total_consumed;
                    }
                    ready_ = true;
                    break;
                }
            }
        }
        return total_consumed;
    }

    auto response_parser::consume(const char* data, std::size_t len)
        -> std::expected<std::size_t, std::error_code>
    {
        if (ready_) return std::size_t{0};
        buf_.append(data, len);
        const std::size_t total_consumed = len;
        while (!ready_)
        {
            switch (state_)
            {
            case state::status_line:
                {
                    const auto pos = detail::find_crlf(buf_.data(), buf_.size());
                    if (pos == std::string_view::npos)
                    {
                        if (buf_.size() > max_header_size)
                            return std::unexpected(make_error_code(http_errc::header_too_large));
                        return total_consumed;
                    }
                    auto parsed = parse_status_line({buf_.data(), pos});
                    if (!parsed) return std::unexpected(parsed.error());
                    header_bytes_ += pos + 2;
                    buf_.erase(0, pos + 2);
                    state_ = state::headers;
                    break;
                }
            case state::headers:
                {
                    const auto pos = detail::find_crlf(buf_.data(), buf_.size());
                    if (pos == std::string_view::npos)
                    {
                        if (header_bytes_ + buf_.size() > max_header_size)
                            return std::unexpected(make_error_code(http_errc::header_too_large));
                        return total_consumed;
                    }
                    if (pos == 0)
                    {
                        buf_.erase(0, 2);
                        header_bytes_ += 2;
                        if (!prepare_body()) ready_ = true;
                        else state_ = state::body;
                        break;
                    }
                    auto parsed = parse_header_line({buf_.data(), pos});
                    if (!parsed) return std::unexpected(parsed.error());
                    header_bytes_ += pos + 2;
                    buf_.erase(0, pos + 2);
                    break;
                }
            case state::body:
                {
                    if (chunked_)
                    {
                        auto parsed = process_chunked_body();
                        if (!parsed) return std::unexpected(parsed.error());
                        if (!*parsed) return total_consumed;
                    }
                    else
                    {
                        const auto take = std::min(buf_.size(), body_bytes_remaining_);
                        body_.append(buf_.data(), take);
                        buf_.erase(0, take);
                        body_bytes_remaining_ -= take;
                        if (body_bytes_remaining_ != 0) return total_consumed;
                    }
                    ready_ = true;
                    break;
                }
            }
        }
        return total_consumed;
    }

    auto response_parser::ready() const noexcept -> bool { return ready_; }
    auto response_parser::version() const noexcept -> http_version { return version_; }
    auto response_parser::status_code() const noexcept -> int { return status_code_; }
    auto response_parser::status_message() const noexcept -> std::string_view { return status_msg_; }
    auto response_parser::headers() const noexcept -> const header_map& { return headers_; }
    auto response_parser::body() const noexcept -> std::string_view { return body_; }

    auto response_parser::get_header(std::string_view k) const -> std::string_view
    {
        auto it = headers_.find(std::string(k));
        return it == headers_.end() ? std::string_view{} : std::string_view{it->second};
    }

    void response_parser::reset() noexcept
    {
        buf_.clear();
        version_ = http_version::http_1_1;
        status_code_ = 0;
        status_msg_.clear();
        headers_.clear();
        body_.clear();
        state_ = state::status_line;
        header_bytes_ = body_bytes_remaining_ = 0;
        chunked_ = ready_ = false;
    }

    auto response_parser::parse_status_line(std::string_view l) -> std::expected<void, std::error_code>
    {
        auto a = l.find(' ');
        if (a == std::string_view::npos)return std::unexpected(make_error_code(http_errc::invalid_status_line));
        auto v = string_to_version(l.substr(0, a));
        if (!v)return std::unexpected(make_error_code(http_errc::invalid_version));
        version_ = *v;
        auto r = l.substr(a + 1);
        auto b = r.find(' ');
        auto code = b == std::string_view::npos ? r : r.substr(0, b);
        if (b != std::string_view::npos)status_msg_ = std::string(r.substr(b + 1));
        auto [p,e] = std::from_chars(code.data(), code.data() + code.size(), status_code_);
        if (e != std::errc{})return std::unexpected(make_error_code(http_errc::invalid_status_line));
        return {};
    }

    auto response_parser::parse_header_line(std::string_view l) -> std::expected<void, std::error_code>
    {
        auto c = l.find(':');
        if (c == std::string_view::npos)return std::unexpected(make_error_code(http_errc::invalid_header));
        auto k = detail::trim(l.substr(0, c));
        auto v = detail::trim(l.substr(c + 1));
        if (k.empty())return std::unexpected(make_error_code(http_errc::invalid_header));
        auto it = headers_.find(std::string(k));
        if (it == headers_.end())headers_.emplace(std::string(k), std::string(v));
        else
        {
            it->second += ", ";
            it->second += v;
        }
        return {};
    }

    auto response_parser::prepare_body() -> bool
    {
        auto t = get_header("Transfer-Encoding");
        if (t.find("chunked") != std::string_view::npos)
        {
            chunked_ = true;
            return true;
        }
        auto c = get_header("Content-Length");
        std::size_t n{};
        auto [p,e] = std::from_chars(c.data(), c.data() + c.size(), n);
        if (!c.empty() && e == std::errc{} && n)
        {
            if (n > max_body_size)return false;
            body_bytes_remaining_ = n;
            body_.reserve(n);
            return true;
        }
        return false;
    }

    auto response_parser::process_chunked_body() -> std::expected<bool, std::error_code>
    {
        for (;;)
        {
            auto p = detail::find_crlf(buf_.data(), buf_.size());
            if (p == std::string_view::npos)return false;
            std::size_t n{};
            auto [q,e] = std::from_chars(buf_.data(), buf_.data() + p, n, 16);
            if (e != std::errc{})return std::unexpected(make_error_code(http_errc::invalid_chunk));
            if (!n)
            {
                buf_.erase(0, p + 2);
                if (buf_.starts_with("\r\n"))buf_.erase(0, 2);
                return true;
            }
            auto start = p + 2;
            if (buf_.size() < start + n + 2)return false;
            body_.append(buf_.data() + start, n);
            buf_.erase(0, start + n + 2);
            if (body_.size() > max_body_size)return std::unexpected(make_error_code(http_errc::body_too_large));
        }
    }
} // namespace cnetmod::http
