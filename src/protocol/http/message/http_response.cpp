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
        // Names are almost always written in their canonical case.
        if (std::memcmp(left.data(), right.data(), left.size()) == 0)
            return true;
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
} // namespace

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
            active_common_headers_ |= static_cast<std::uint16_t>(1U << *slot);
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
        active_common_headers_ &=
            static_cast<std::uint16_t>(~(1U << *slot));
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
    shared_body_.reset();
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
    shared_body_.reset();
    body_source_.reset();
    std::array<char, 24> length{};
    const auto [end, error] = std::to_chars(
        length.data(), length.data() + length.size(), body.size());
    if (error == std::errc{})
        set_common_header(0, {length.data(), end});
    body_ = std::move(body);
    return *this;
}

auto response::set_shared_body(std::shared_ptr<const std::string> body)
    -> response&
{
    body_.clear();
    body_source_.reset();
    shared_body_ = std::move(body);
    std::array<char, 24> length{};
    const auto size = shared_body_ ? shared_body_->size() : 0U;
    const auto [end, error] = std::to_chars(
        length.data(), length.data() + length.size(), size);
    if (error == std::errc{})
        set_common_header(0, {length.data(), end});
    return *this;
}

auto response::set_body_preserve_headers(std::string body) -> response&
{
    shared_body_.reset();
    body_source_.reset();
    body_ = std::move(body);
    return *this;
}

auto response::set_body_source(std::shared_ptr<response_body_source> source)
    -> response&
{
    body_.clear();
    shared_body_.reset();
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
    auto active = active_common_headers_;
    while (active != 0)
    {
        const auto slot = static_cast<std::size_t>(std::countr_zero(active));
        auto& header = common_headers_[slot];
        header.value.clear();
        header.present = false;
        active &= static_cast<std::uint16_t>(active - 1U);
    }
    active_common_headers_ = 0;
    invalidate_headers_snapshot();
    trailers_.clear();
    body_.clear();
    shared_body_.reset();
    body_source_.reset();
    preencoded_h2_headers_.reset();
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
    return shared_body_ ? std::string_view{*shared_body_}
                        : std::string_view{body_};
}

auto response::take_headers() -> header_map
{
    materialize_headers();
    headers_snapshot_valid_ = false;
    return std::move(headers_snapshot_);
}

auto response::header_count() const noexcept -> std::size_t
{
    return headers_.size() + (has_cached_date_header_ ? 1U : 0U) +
        static_cast<std::size_t>(std::popcount(active_common_headers_));
}

auto response::take_trailers() noexcept -> header_map
{
    return std::move(trailers_);
}

auto response::take_body() noexcept -> std::string
{
    return std::move(body_);
}

auto response::shared_body() const noexcept
    -> const std::shared_ptr<const std::string>&
{
    return shared_body_;
}

auto response::take_shared_body() noexcept
    -> std::shared_ptr<const std::string>
{
    return std::exchange(shared_body_, {});
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

auto response::set_preencoded_h2_headers(
    std::shared_ptr<const preencoded_header_block> block) -> response&
{
    preencoded_h2_headers_ = std::move(block);
    return *this;
}

auto response::preencoded_h2_headers() const noexcept
    -> const std::shared_ptr<const preencoded_header_block>&
{
    return preencoded_h2_headers_;
}

auto response::take_preencoded_h2_headers() noexcept
    -> std::shared_ptr<const preencoded_header_block>
{
    return std::exchange(preencoded_h2_headers_, {});
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
    active_common_headers_ &= static_cast<std::uint16_t>(~(1U << 6U));
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
    const auto body_view = body();
    append_serialized_headers_to(output, body_view.size());
    output += body_view;
}

void response::append_serialized_headers_to(std::string& output,
    std::size_t reserve_extra) const
{
    const auto reserve_size = 64U + reserve_extra +
        (has_cached_date_header_ ? cached_date_header_.size() + 8U : 0U) +
        std::accumulate(headers_.begin(), headers_.end(), std::size_t{},
            [](std::size_t total, const auto& header)
            {
                return total + header.first.size() + header.second.size() + 4U;
            });
    std::size_t common_size{};
    auto active = active_common_headers_;
    while (active != 0)
    {
        const auto slot = static_cast<std::size_t>(std::countr_zero(active));
        common_size += common_header_name(slot).size() +
            common_headers_[slot].value.size() + 4U;
        active &= static_cast<std::uint16_t>(active - 1U);
    }
    output.reserve(output.size() + reserve_size + common_size);
    if (version_ == http_version::http_1_1 && status_msg_.empty() && status_code_ == 200)
    {
        // Hot path: the status line and every header are copied straight
        // into storage sized exactly once, with no per-piece append calls.
        constexpr std::string_view status_line{"HTTP/1.1 200 OK\r\n"};
        constexpr std::string_view date_prefix{"Date: "};
        std::size_t total = status_line.size() + 2U + common_size;
        if (has_cached_date_header_)
            total += date_prefix.size() + cached_date_header_.size() + 2U;
        for (const auto& [key, value] : headers_)
            total += key.size() + value.size() + 4U;
        const auto start = output.size();
        output.resize_and_overwrite(start + total,
            [&](char* buffer, std::size_t) noexcept
            {
                char* cursor = buffer + start;
                const auto put = [&cursor](std::string_view text) noexcept
                {
                    std::memcpy(cursor, text.data(), text.size());
                    cursor += text.size();
                };
                put(status_line);
                if (has_cached_date_header_)
                {
                    put(date_prefix);
                    put({cached_date_header_.data(), cached_date_header_.size()});
                    put("\r\n");
                }
                auto remaining = active_common_headers_;
                while (remaining != 0)
                {
                    const auto slot = static_cast<std::size_t>(std::countr_zero(remaining));
                    put(common_header_name(slot));
                    put(": ");
                    put(common_headers_[slot].value);
                    put("\r\n");
                    remaining &= static_cast<std::uint16_t>(remaining - 1U);
                }
                for (const auto& [key, value] : headers_)
                {
                    put(key);
                    put(": ");
                    put(value);
                    put("\r\n");
                }
                put("\r\n");
                return start + total;
            });
        return;
    }
    if (version_ == http_version::http_1_1 && status_msg_.empty())
    {
        switch (status_code_)
        {
        case 200:
            output += "HTTP/1.1 200 OK\r\n";
            break;
        case 201:
            output += "HTTP/1.1 201 Created\r\n";
            break;
        case 204:
            output += "HTTP/1.1 204 No Content\r\n";
            break;
        case 400:
            output += "HTTP/1.1 400 Bad Request\r\n";
            break;
        case 401:
            output += "HTTP/1.1 401 Unauthorized\r\n";
            break;
        case 403:
            output += "HTTP/1.1 403 Forbidden\r\n";
            break;
        case 404:
            output += "HTTP/1.1 404 Not Found\r\n";
            break;
        case 405:
            output += "HTTP/1.1 405 Method Not Allowed\r\n";
            break;
        case 429:
            output += "HTTP/1.1 429 Too Many Requests\r\n";
            break;
        case 500:
            output += "HTTP/1.1 500 Internal Server Error\r\n";
            break;
        case 503:
            output += "HTTP/1.1 503 Service Unavailable\r\n";
            break;
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
    active = active_common_headers_;
    while (active != 0)
    {
        const auto slot = static_cast<std::size_t>(std::countr_zero(active));
        const auto& header = common_headers_[slot];
        output += common_header_name(slot);
        output += ": ";
        output += header.value;
        output += "\r\n";
        active &= static_cast<std::uint16_t>(active - 1U);
    }
    for (const auto& [key, value] : headers_)
    {
        output += key;
        output += ": ";
        output += value;
        output += "\r\n";
    }
    output += "\r\n";
}

auto response::common_header_slot(std::string_view key) noexcept
    -> std::optional<std::size_t>
{
    switch (key.size())
    {
    case 4:
        return header_equal(key, "Date") ? std::optional<std::size_t>{6}
                                         : std::nullopt;
    case 5:
        return header_equal(key, "Allow") ? std::optional<std::size_t>{8}
                                          : std::nullopt;
    case 6:
        return header_equal(key, "Server") ? std::optional<std::size_t>{4}
                                           : std::nullopt;
    case 8:
        return header_equal(key, "Location") ? std::optional<std::size_t>{7}
                                             : std::nullopt;
    case 10:
        if (header_equal(key, "Connection"))
            return 2;
        if (header_equal(key, "X-Streamed"))
            return 5;
        return std::nullopt;
    case 11:
        return header_equal(key, "Retry-After")
            ? std::optional<std::size_t>{9}
            : std::nullopt;
    case 12:
        return header_equal(key, "Content-Type")
            ? std::optional<std::size_t>{1}
            : std::nullopt;
    case 14:
        return header_equal(key, "Content-Length")
            ? std::optional<std::size_t>{0}
            : std::nullopt;
    case 17:
        return header_equal(key, "Transfer-Encoding")
            ? std::optional<std::size_t>{3}
            : std::nullopt;
    default:
        break;
    }
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
    auto active = active_common_headers_;
    while (active != 0)
    {
        const auto slot = static_cast<std::size_t>(std::countr_zero(active));
        headers_snapshot_[std::string{common_header_name(slot)}] =
            common_headers_[slot].value;
        active &= static_cast<std::uint16_t>(active - 1U);
    }
    headers_snapshot_valid_ = true;
}

auto response::set_common_header(std::size_t slot, std::string_view value)
    -> response&
{
    auto& header = common_headers_[slot];
    header.value.assign(value);
    header.present = true;
    active_common_headers_ |= static_cast<std::uint16_t>(1U << slot);
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
    active_common_headers_ |= static_cast<std::uint16_t>(1U << slot);
    if (slot == 6)
        has_cached_date_header_ = false;
    invalidate_headers_snapshot();
    return *this;
}

} // namespace cnetmod::http
