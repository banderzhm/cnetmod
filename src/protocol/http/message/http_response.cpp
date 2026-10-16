module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.http;

import std;
import :response;

namespace cnetmod::http {

namespace {
    auto header_equal(std::string_view left, std::string_view right) noexcept
        -> bool
    {
        if (left.size() != right.size())
            return false;
        for (std::size_t index{}; index < left.size(); ++index)
        {
            const auto lower = [](unsigned char value) noexcept
            {
                return value >= 'A' && value <= 'Z'
                    ? static_cast<unsigned char>(value + ('a' - 'A'))
                    : value;
            };
            if (lower(static_cast<unsigned char>(left[index])) !=
                lower(static_cast<unsigned char>(right[index])))
                return false;
        }
        return true;
    }
}

response::response() = default;

response::response(int status_code, http_version version)
    : status_code_(status_code), version_(version) {}

auto response::set_status(int code) noexcept -> response&
{
    status_code_ = code;
    return *this;
}

auto response::set_status_message(std::string_view msg) -> response&
{
    status_msg_ = std::string(msg);
    return *this;
}

auto response::set_version(http_version version) noexcept -> response&
{
    version_ = version;
    return *this;
}

auto response::set_header(std::string_view key, std::string_view value)
    -> response&
{
    if (const auto slot = common_header_slot(key))
        return set_common_header(*slot, value);
    if (header_equal(key, "Date"))
        has_cached_date_header_ = false;
    headers_[std::string(key)] = std::string(value);
    invalidate_headers_snapshot();
    return *this;
}

auto response::set_header_owned(std::string_view key, std::string value)
    -> response&
{
    if (const auto slot = common_header_slot(key))
        return set_common_header_owned(*slot, std::move(value));
    if (header_equal(key, "Date"))
        has_cached_date_header_ = false;
    headers_[std::string(key)] = std::move(value);
    invalidate_headers_snapshot();
    return *this;
}

auto response::append_header(std::string_view key, std::string_view value)
    -> response&
{
    if (const auto slot = common_header_slot(key))
    {
        auto& header = common_headers_[*slot];
        if (header.present)
        {
            header.value += ", ";
            header.value += value;
        }
        else
        {
            header.value.assign(value);
            header.present = true;
        }
        if (*slot == 6)
            has_cached_date_header_ = false;
        invalidate_headers_snapshot();
        return *this;
    }
    if (header_equal(key, "Date") && has_cached_date_header_)
    {
        headers_["Date"] = std::string{cached_date_header_.data(),
            cached_date_header_.size()};
        has_cached_date_header_ = false;
    }
    auto it = headers_.find(key);
    if (it != headers_.end())
    {
        it->second += ", ";
        it->second += value;
    }
    else
    {
        headers_[std::string(key)] = std::string(value);
    }
    invalidate_headers_snapshot();
    return *this;
}

auto response::set_trailer(std::string_view key, std::string_view value)
    -> response&
{
    trailers_[std::string(key)] = std::string(value);
    return *this;
}

auto response::append_trailer(std::string_view key, std::string_view value)
    -> response&
{
    auto it = trailers_.find(std::string(key));
    if (it != trailers_.end())
    {
        it->second += ", ";
        it->second += value;
    }
    else
    {
        trailers_[std::string(key)] = std::string(value);
    }
    return *this;
}

auto response::remove_header(std::string_view key) -> response&
{
    if (const auto slot = common_header_slot(key))
    {
        common_headers_[*slot].present = false;
        common_headers_[*slot].value.clear();
        if (*slot == 6)
            has_cached_date_header_ = false;
        invalidate_headers_snapshot();
        return *this;
    }
    if (header_equal(key, "Date"))
        has_cached_date_header_ = false;
    headers_.erase(key);
    invalidate_headers_snapshot();
    return *this;
}

auto response::set_body(std::string_view body) -> response&
{
    body_source_.reset();
    body_.assign(body);
    std::array<char, 24> length{};
    const auto [end, error] = std::to_chars(
        length.data(), length.data() + length.size(), body_.size());
    if (error == std::errc{})
        set_common_header(0, {length.data(), end});
    return *this;
}

auto response::set_body(std::string body) -> response&
{
    body_source_.reset();
    std::array<char, 24> length{};
    const auto [end, error] = std::to_chars(
        length.data(), length.data() + length.size(), body.size());
    if (error == std::errc{})
        set_common_header(0, {length.data(), end});
    body_ = std::move(body);
    return *this;
}

auto response::set_body_preserve_headers(std::string body) -> response&
{
    body_source_.reset();
    body_ = std::move(body);
    return *this;
}

auto response::set_body_source(std::shared_ptr<response_body_source> source)
    -> response&
{
    body_.clear();
    body_source_ = std::move(source);
    remove_header("Content-Length");
    if (body_source_ && body_source_->content_length())
        set_header("Content-Length",
            std::to_string(*body_source_->content_length()));
    return *this;
}

void response::reset(int status_code, http_version version) noexcept
{
    status_code_ = status_code;
    status_msg_.clear();
    version_ = version;
    headers_.clear();
    for (auto& header : common_headers_)
    {
        header.value.clear();
        header.present = false;
    }
    invalidate_headers_snapshot();
    trailers_.clear();
    body_.clear();
    body_source_.reset();
    has_cached_date_header_ = false;
}

auto response::set_cookie(std::string_view name, std::string_view value,
    std::string_view domain, std::string_view path,
    std::optional<std::chrono::seconds> max_age,
    bool secure, bool http_only) -> response&
{
    cookie c;
    c.name = std::string(name);
    c.value = std::string(value);
    if (!domain.empty())
        c.domain = std::string(domain);
    c.path = std::string(path);
    c.max_age = max_age;
    c.secure = secure;
    c.http_only = http_only;
    return append_header("Set-Cookie", c.to_set_cookie_header());
}

auto response::set_cookie(const cookie& c) -> response&
{
    return append_header("Set-Cookie", c.to_set_cookie_header());
}

auto response::status_code() const noexcept -> int
{
    return status_code_;
}

auto response::version() const noexcept -> http_version
{
    return version_;
}

auto response::headers() const -> const header_map&
{
    materialize_headers();
    return headers_snapshot_;
}

auto response::trailers() const noexcept -> const header_map&
{
    return trailers_;
}

auto response::body() const noexcept -> std::string_view
{
    return body_;
}

auto response::take_body() noexcept -> std::string
{
    return std::move(body_);
}

auto response::body_source() const noexcept
    -> const std::shared_ptr<response_body_source>&
{
    return body_source_;
}

auto response::take_body_source() noexcept
    -> std::shared_ptr<response_body_source>
{
    return std::exchange(body_source_, {});
}

auto response::get_header(std::string_view key) const -> std::string_view
{
    if (const auto slot = common_header_slot(key))
    {
        const auto& header = common_headers_[*slot];
        if (header.present)
            return header.value;
        if (*slot == 6 && has_cached_date_header_)
            return {cached_date_header_.data(), cached_date_header_.size()};
        return {};
    }
    if (header_equal(key, "Date") && has_cached_date_header_)
        return {cached_date_header_.data(), cached_date_header_.size()};
    auto it = headers_.find(key);
    return it != headers_.end() ? std::string_view(it->second)
                                : std::string_view{};
}

auto response::serialize() const -> std::string
{
    std::string out;
    serialize_to(out);
    return out;
}

void response::set_cached_date_header(std::array<char, 29U> value) noexcept
{
    cached_date_header_ = value;
    has_cached_date_header_ = true;
    common_headers_[6].present = false;
    common_headers_[6].value.clear();
    invalidate_headers_snapshot();
}

auto response::cached_date_header() const noexcept -> std::string_view
{
    return has_cached_date_header_
        ? std::string_view{cached_date_header_.data(), cached_date_header_.size()}
        : std::string_view{};
}

void response::serialize_to(std::string& output) const
{
    output.clear();
    append_serialized_to(output);
}

void response::append_serialized_to(std::string& output) const
{
    const auto reserve_size = 64U + body_.size() +
        (has_cached_date_header_ ? cached_date_header_.size() + 8U : 0U) +
        std::accumulate(headers_.begin(), headers_.end(), std::size_t{},
            [](std::size_t total, const auto& header)
            {
                return total + header.first.size() + header.second.size() + 4U;
            });
    std::size_t common_size{};
    for (std::size_t slot{}; slot < common_headers_.size(); ++slot)
        if (common_headers_[slot].present)
            common_size += common_header_name(slot).size() +
                common_headers_[slot].value.size() + 4U;
    output.reserve(output.size() + reserve_size + common_size);
    if (version_ == http_version::http_1_1 && status_msg_.empty())
    {
        switch (status_code_)
        {
        case 200: output += "HTTP/1.1 200 OK\r\n"; break;
        case 201: output += "HTTP/1.1 201 Created\r\n"; break;
        case 204: output += "HTTP/1.1 204 No Content\r\n"; break;
        case 400: output += "HTTP/1.1 400 Bad Request\r\n"; break;
        case 401: output += "HTTP/1.1 401 Unauthorized\r\n"; break;
        case 403: output += "HTTP/1.1 403 Forbidden\r\n"; break;
        case 404: output += "HTTP/1.1 404 Not Found\r\n"; break;
        case 405: output += "HTTP/1.1 405 Method Not Allowed\r\n"; break;
        case 429: output += "HTTP/1.1 429 Too Many Requests\r\n"; break;
        case 500: output += "HTTP/1.1 500 Internal Server Error\r\n"; break;
        case 503: output += "HTTP/1.1 503 Service Unavailable\r\n"; break;
        default:
            output += "HTTP/1.1 ";
            output += std::to_string(status_code_);
            output += ' ';
            output += status_reason(status_code_);
            output += "\r\n";
            break;
        }
    }
    else
    {
        output += version_to_string(version_);
        output += ' ';
        output += std::to_string(status_code_);
        output += ' ';
        output += status_msg_.empty() ? status_reason(status_code_)
                                      : status_msg_;
        output += "\r\n";
    }
    if (has_cached_date_header_)
    {
        output += "Date: ";
        output.append(cached_date_header_.data(), cached_date_header_.size());
        output += "\r\n";
    }
    for (std::size_t slot{}; slot < common_headers_.size(); ++slot)
    {
        const auto& header = common_headers_[slot];
        if (!header.present)
            continue;
        output += common_header_name(slot);
        output += ": ";
        output += header.value;
        output += "\r\n";
    }
    for (const auto& [key, value] : headers_)
    {
        output += key;
        output += ": ";
        output += value;
        output += "\r\n";
    }
    output += "\r\n";
    output += body_;
}

auto response::common_header_slot(std::string_view key) noexcept
    -> std::optional<std::size_t>
{
    constexpr std::array<std::string_view, common_header_count> names{
        "Content-Length", "Content-Type", "Connection", "Transfer-Encoding",
        "Server", "X-Streamed", "Date", "Location", "Allow", "Retry-After"};
    for (std::size_t slot{}; slot < names.size(); ++slot)
        if (header_equal(key, names[slot]))
            return slot;
    return std::nullopt;
}

auto response::common_header_name(std::size_t slot) noexcept
    -> std::string_view
{
    constexpr std::array<std::string_view, common_header_count> names{
        "Content-Length", "Content-Type", "Connection", "Transfer-Encoding",
        "Server", "X-Streamed", "Date", "Location", "Allow", "Retry-After"};
    return names[slot];
}

void response::invalidate_headers_snapshot() noexcept
{
    headers_snapshot_valid_ = false;
}

void response::materialize_headers() const
{
    if (headers_snapshot_valid_)
        return;
    headers_snapshot_ = headers_;
    if (has_cached_date_header_)
        headers_snapshot_["Date"] = std::string{
            cached_date_header_.data(), cached_date_header_.size()};
    for (std::size_t slot{}; slot < common_headers_.size(); ++slot)
        if (common_headers_[slot].present)
            headers_snapshot_[std::string{common_header_name(slot)}] =
                common_headers_[slot].value;
    headers_snapshot_valid_ = true;
}

auto response::set_common_header(std::size_t slot, std::string_view value)
    -> response&
{
    auto& header = common_headers_[slot];
    header.value.assign(value);
    header.present = true;
    if (slot == 6)
        has_cached_date_header_ = false;
    invalidate_headers_snapshot();
    return *this;
}

auto response::set_common_header_owned(std::size_t slot, std::string value)
    -> response&
{
    auto& header = common_headers_[slot];
    header.value = std::move(value);
    header.present = true;
    if (slot == 6)
        has_cached_date_header_ = false;
    invalidate_headers_snapshot();
    return *this;
}

} // namespace cnetmod::http
