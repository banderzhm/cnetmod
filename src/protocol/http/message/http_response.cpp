module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.http;

import std;
import :response;

namespace cnetmod::http {

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
    headers_[std::string(key)] = std::string(value);
    return *this;
}

auto response::append_header(std::string_view key, std::string_view value)
    -> response&
{
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
    headers_.erase(key);
    return *this;
}

auto response::set_body(std::string_view body) -> response&
{
    body_ = std::string(body);
    headers_["Content-Length"] = std::to_string(body_.size());
    return *this;
}

auto response::set_body(std::string body) -> response&
{
    headers_["Content-Length"] = std::to_string(body.size());
    body_ = std::move(body);
    return *this;
}

auto response::set_body_preserve_headers(std::string body) -> response&
{
    body_ = std::move(body);
    return *this;
}

void response::reset(int status_code, http_version version) noexcept
{
    status_code_ = status_code;
    status_msg_.clear();
    version_ = version;
    headers_.clear();
    trailers_.clear();
    body_.clear();
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

auto response::headers() const noexcept -> const header_map&
{
    return headers_;
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

auto response::get_header(std::string_view key) const -> std::string_view
{
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

void response::serialize_to(std::string& output) const
{
    output.clear();
    const auto reserve_size = 64U + body_.size() +
        std::accumulate(headers_.begin(), headers_.end(), std::size_t{},
            [](std::size_t total, const auto& header)
            {
                return total + header.first.size() + header.second.size() + 4U;
            });
    output.reserve(reserve_size);
    output += version_to_string(version_);
    output += ' ';
    output += std::to_string(status_code_);
    output += ' ';
    output += status_msg_.empty() ? status_reason(status_code_) : status_msg_;
    output += "\r\n";
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

} // namespace cnetmod::http
