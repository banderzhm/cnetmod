module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.http.semantics;

import std;
import cnetmod.coro.task;

namespace cnetmod::http {

auto guess_mime_type(std::string_view extension) noexcept -> std::string_view
{
    if (!extension.empty() && extension.front() == '.')
        extension.remove_prefix(1);

    if (extension == "html" || extension == "htm")
        return "text/html; charset=utf-8";
    if (extension == "css")
        return "text/css; charset=utf-8";
    if (extension == "js")
        return "application/javascript; charset=utf-8";
    if (extension == "json")
        return "application/json; charset=utf-8";
    if (extension == "xml")
        return "application/xml; charset=utf-8";
    if (extension == "txt")
        return "text/plain; charset=utf-8";
    if (extension == "csv")
        return "text/csv; charset=utf-8";
    if (extension == "md")
        return "text/markdown; charset=utf-8";
    if (extension == "png")
        return "image/png";
    if (extension == "jpg" || extension == "jpeg")
        return "image/jpeg";
    if (extension == "gif")
        return "image/gif";
    if (extension == "svg")
        return "image/svg+xml";
    if (extension == "ico")
        return "image/x-icon";
    if (extension == "webp")
        return "image/webp";
    if (extension == "bmp")
        return "image/bmp";
    if (extension == "mp3")
        return "audio/mpeg";
    if (extension == "mp4")
        return "video/mp4";
    if (extension == "webm")
        return "video/webm";
    if (extension == "ogg")
        return "audio/ogg";
    if (extension == "wav")
        return "audio/wav";
    if (extension == "pdf")
        return "application/pdf";
    if (extension == "zip")
        return "application/zip";
    if (extension == "gz" || extension == "gzip")
        return "application/gzip";
    if (extension == "tar")
        return "application/x-tar";
    if (extension == "wasm")
        return "application/wasm";
    if (extension == "woff")
        return "font/woff";
    if (extension == "woff2")
        return "font/woff2";
    if (extension == "ttf")
        return "font/ttf";
    if (extension == "otf")
        return "font/otf";
    return "application/octet-stream";
}

auto accepts_content_encoding(std::string_view header,
    std::string_view encoding) noexcept -> bool
{
    const auto equal_ascii = [](std::string_view left,
                                 std::string_view right) noexcept
    {
        return left.size() == right.size() &&
            std::ranges::equal(left, right, [](char lhs, char rhs)
                {
                    const auto lower = [](char value) noexcept
                    {
                        return value >= 'A' && value <= 'Z'
                            ? static_cast<char>(value - 'A' + 'a')
                            : value;
                    };
                    return lower(lhs) == lower(rhs);
                });
    };
    const auto trim = [](std::string_view value) noexcept
    {
        while (!value.empty() &&
            (value.front() == ' ' || value.front() == '\t'))
            value.remove_prefix(1);
        while (!value.empty() &&
            (value.back() == ' ' || value.back() == '\t'))
            value.remove_suffix(1);
        return value;
    };

    const auto quality_enabled = [trim](std::string_view value) noexcept
    {
        value = trim(value);
        if (value.empty() || value.front() != '0')
            return true;
        value.remove_prefix(1);
        if (!value.empty() && value.front() == '.')
            value.remove_prefix(1);
        return std::ranges::any_of(value, [](char digit)
            {
                return digit != '0';
            });
    };

    bool explicit_found{};
    bool explicit_enabled{};
    bool wildcard_enabled{};
    while (!header.empty())
    {
        const auto comma = header.find(',');
        const auto item = header.substr(0, comma);
        const auto semicolon = item.find(';');
        const auto name = trim(item.substr(0, semicolon));
        auto enabled = true;
        if (semicolon != std::string_view::npos)
        {
            auto parameters = item.substr(semicolon + 1);
            while (!parameters.empty())
            {
                const auto separator = parameters.find(';');
                const auto parameter = trim(parameters.substr(0, separator));
                const auto equals = parameter.find('=');
                if (equals != std::string_view::npos &&
                    equal_ascii(trim(parameter.substr(0, equals)), "q"))
                {
                    const auto quality = trim(parameter.substr(equals + 1));
                    enabled = quality_enabled(quality);
                }
                if (separator == std::string_view::npos)
                    break;
                parameters.remove_prefix(separator + 1);
            }
        }
        if (equal_ascii(name, encoding))
        {
            explicit_found = true;
            explicit_enabled = explicit_enabled || enabled;
        }
        else if (name == "*")
            wildcard_enabled = wildcard_enabled || enabled;
        if (comma == std::string_view::npos)
            break;
        header.remove_prefix(comma + 1);
    }
    return explicit_found ? explicit_enabled : wildcard_enabled;
}

request_body_stream::request_body_stream(std::size_t capacity,
    std::size_t max_bytes)
    : chunks_(capacity), max_bytes_(max_bytes) {}

auto request_body_stream::receive() -> task<std::optional<request_body_chunk>>
{
    auto chunk = co_await chunks_.receive();
    if (chunk && consumption_observer_)
        consumption_observer_(chunk->size());
    co_return chunk;
}

auto request_body_stream::send(request_body_chunk chunk) -> task<bool>
{
    const auto bytes = chunk.size();
    if (!can_accept(bytes))
        co_return false;
    if (!(co_await chunks_.send(std::move(chunk))))
        co_return false;
    received_bytes_ += bytes;
    co_return true;
}

auto request_body_stream::try_receive() -> std::optional<request_body_chunk>
{
    auto chunk = chunks_.try_receive();
    if (chunk && consumption_observer_)
        consumption_observer_(chunk->size());
    return chunk;
}

auto request_body_stream::push(request_body_chunk chunk) -> bool
{
    const auto bytes = chunk.size();
    if (!can_accept(bytes) || !chunks_.try_send(std::move(chunk)))
        return false;
    received_bytes_ += bytes;
    return true;
}

void request_body_stream::close() noexcept
{
    chunks_.close();
}

void request_body_stream::fail(std::error_code error) noexcept
{
    if (!error_)
        error_ = error;
    chunks_.close();
}

auto request_body_stream::is_closed() const noexcept -> bool
{
    return chunks_.is_closed();
}

auto request_body_stream::received_bytes() const noexcept -> std::size_t
{
    return received_bytes_;
}

auto request_body_stream::error() const noexcept -> std::error_code
{
    return error_;
}

void request_body_stream::observe_consumption(consumption_observer observer)
{
    consumption_observer_ = std::move(observer);
}

void request_body_stream::constrain_limit(std::size_t max_bytes) noexcept
{
    max_bytes_ = std::min(max_bytes_, max_bytes);
    if (received_bytes_ > max_bytes_)
        fail(std::make_error_code(std::errc::message_size));
}

auto request_body_stream::can_accept(std::size_t bytes) noexcept -> bool
{
    if (chunks_.is_closed())
        return false;
    if (bytes > max_bytes_ - std::min(max_bytes_, received_bytes_))
    {
        fail(std::make_error_code(std::errc::message_size));
        return false;
    }
    return true;
}

auto case_insensitive_less::operator()(std::string_view left,
    std::string_view right) const noexcept
    -> bool
{
    const auto length = std::min(left.size(), right.size());
    for (std::size_t index = 0; index < length; ++index)
    {
        const auto lhs = static_cast<unsigned char>(left[index]);
        const auto rhs = static_cast<unsigned char>(right[index]);
        const auto normalized_lhs = lhs >= 'A' && lhs <= 'Z' ? lhs + 32 : lhs;
        const auto normalized_rhs = rhs >= 'A' && rhs <= 'Z' ? rhs + 32 : rhs;
        if (normalized_lhs != normalized_rhs)
            return normalized_lhs < normalized_rhs;
    }
    return left.size() < right.size();
}

auto url::parse(std::string_view input) -> std::expected<url, std::string>
{
    url result;
    const auto scheme_end = input.find("://");
    if (scheme_end == std::string_view::npos)
        return std::unexpected("missing scheme");
    result.scheme = input.substr(0, scheme_end);
    auto remainder = input.substr(scheme_end + 3);

    if (const auto fragment = remainder.find('#');
        fragment != std::string_view::npos)
    {
        result.fragment = remainder.substr(fragment + 1);
        remainder = remainder.substr(0, fragment);
    }
    if (const auto query = remainder.find('?'); query != std::string_view::npos)
    {
        result.query = remainder.substr(query + 1);
        remainder = remainder.substr(0, query);
    }
    if (const auto path = remainder.find('/'); path != std::string_view::npos)
    {
        result.path = remainder.substr(path);
        remainder = remainder.substr(0, path);
    }
    else
    {
        result.path = "/";
    }

    std::string_view port_text;
    if (remainder.starts_with('['))
    {
        const auto bracket = remainder.find(']');
        if (bracket == std::string_view::npos)
            return std::unexpected("unclosed IPv6 bracket");
        result.host = remainder.substr(1, bracket - 1);
        if (bracket + 1 < remainder.size() && remainder[bracket + 1] == ':')
            port_text = remainder.substr(bracket + 2);
    }
    else if (const auto colon = remainder.rfind(':');
        colon != std::string_view::npos)
    {
        result.host = remainder.substr(0, colon);
        port_text = remainder.substr(colon + 1);
    }
    else
    {
        result.host = remainder;
    }
    if (!port_text.empty())
    {
        const auto [_, error] = std::from_chars(
            port_text.data(), port_text.data() + port_text.size(), result.port);
        if (error != std::errc{})
            return std::unexpected("invalid port");
    }
    if (result.port == 0)
        result.port = result.scheme == "https" || result.scheme == "wss" ? 443 : 80;
    return result;
}

auto format_authority_host(std::string_view host) -> std::string
{
    if (host.empty())
        return {};
    if (host.front() == '[' || !host.contains(':'))
        return std::string(host);
    return std::format("[{}]", host);
}

auto format_authority(std::string_view host, std::uint16_t port, bool use_ssl)
    -> std::string
{
    auto result = format_authority_host(host);
    if (!((use_ssl && port == 443) || (!use_ssl && port == 80)))
        result += std::format(":{}", port);
    return result;
}

namespace detail {

    class http_error_category_impl final : public std::error_category
    {
    public:
        auto name() const noexcept -> const char* override
        {
            return "http";
        }

        auto message(int value) const -> std::string override
        {
            switch (static_cast<http_errc>(value))
            {
            case http_errc::success:
                return "success";
            case http_errc::invalid_method:
                return "invalid HTTP method";
            case http_errc::invalid_uri:
                return "invalid URI";
            case http_errc::invalid_version:
                return "invalid HTTP version";
            case http_errc::invalid_status_line:
                return "invalid status line";
            case http_errc::invalid_header:
                return "invalid header";
            case http_errc::header_too_large:
                return "header too large";
            case http_errc::body_too_large:
                return "body too large";
            case http_errc::incomplete_message:
                return "incomplete message";
            case http_errc::need_more_data:
                return "need more data";
            case http_errc::invalid_chunk:
                return "invalid chunk encoding";
            case http_errc::invalid_multipart:
                return "invalid multipart data";
            case http_errc::missing_boundary:
                return "missing multipart boundary";
            case http_errc::invalid_content_disposition:
                return "invalid Content-Disposition";
            case http_errc::unsupported_encoding:
                return "unsupported Content-Transfer-Encoding";
            case http_errc::invalid_url_encoding:
                return "invalid URL encoding";
            }
            return "unknown http error";
        }
    };

    auto http_category_instance() -> const std::error_category&
    {
        static const http_error_category_impl instance;
        return instance;
    }

} // namespace detail

auto make_error_code(http_errc value) noexcept -> std::error_code
{
    return {static_cast<int>(value), detail::http_category_instance()};
}

} // namespace cnetmod::http
