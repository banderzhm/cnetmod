module cnetmod.protocol.http;

import std;
import :parser;
import :semantics;

namespace cnetmod::http {

auto request_parser::consume(const char* data, std::size_t len)
    -> std::expected<std::size_t, std::error_code>
{
    if (ready_) return std::size_t{0};
    buf_.append(data, len);
    const std::size_t total_consumed = len;
    while (!ready_) {
        switch (state_) {
        case state::request_line: {
            const auto pos = detail::find_crlf(buf_.data(), buf_.size());
            if (pos == std::string_view::npos) {
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
        case state::headers: {
            const auto pos = detail::find_crlf(buf_.data(), buf_.size());
            if (pos == std::string_view::npos) {
                if (header_bytes_ + buf_.size() > max_header_size)
                    return std::unexpected(make_error_code(http_errc::header_too_large));
                return total_consumed;
            }
            if (pos == 0) {
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
        case state::body: {
            if (chunked_) {
                auto parsed = process_chunked_body();
                if (!parsed) return std::unexpected(parsed.error());
                if (!*parsed) return total_consumed;
            } else {
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
    while (!ready_) {
        switch (state_) {
        case state::status_line: {
            const auto pos = detail::find_crlf(buf_.data(), buf_.size());
            if (pos == std::string_view::npos) {
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
        case state::headers: {
            const auto pos = detail::find_crlf(buf_.data(), buf_.size());
            if (pos == std::string_view::npos) {
                if (header_bytes_ + buf_.size() > max_header_size)
                    return std::unexpected(make_error_code(http_errc::header_too_large));
                return total_consumed;
            }
            if (pos == 0) {
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
        case state::body: {
            if (chunked_) {
                auto parsed = process_chunked_body();
                if (!parsed) return std::unexpected(parsed.error());
                if (!*parsed) return total_consumed;
            } else {
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

} // namespace cnetmod::http
