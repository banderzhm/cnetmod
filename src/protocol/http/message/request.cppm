module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.http:request;

import std;
import cnetmod.protocol.http.semantics;

namespace cnetmod::http {

// =============================================================================
// request — HTTP Request Builder
// =============================================================================

export class request
{
public:
    request() = default;

    explicit request(http_method method, std::string_view uri,
        http_version version = http_version::http_1_1)
        : method_(method), uri_(uri), version_(version) {}

    // --- Settings ---

    auto& set_method(http_method m) noexcept
    {
        method_ = m;
        return *this;
    }

    auto& set_uri(std::string_view uri)
    {
        uri_ = std::string(uri);
        return *this;
    }

    auto& set_version(http_version v) noexcept
    {
        version_ = v;
        return *this;
    }

    auto& set_header(std::string_view key, std::string_view value)
    {
        headers_[std::string(key)] = std::string(value);
        return *this;
    }

    auto& append_header(std::string_view key, std::string_view value)
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

    auto& remove_header(std::string_view key)
    {
        headers_.erase(key);
        return *this;
    }

    auto& set_body(std::string_view body)
    {
        body_source_.reset();
        body_ = std::string(body);
        // Automatically set Content-Length
        headers_["Content-Length"] = std::to_string(body_.size());
        return *this;
    }

    auto& set_body(std::string body)
    {
        body_source_.reset();
        headers_["Content-Length"] = std::to_string(body.size());
        body_ = std::move(body);
        return *this;
    }

    /// Attach a pull-based streaming request body.  The source is consumed
    /// once by send(); it is intentionally shared so request copies (for
    /// example internal dispatch wrappers) retain the same producer state.
    /// With no length, HTTP/1.1 uses chunked transfer encoding; HTTP/2 and
    /// HTTP/3 send DATA frames until the producer returns nullopt.
    auto& set_body_stream(std::shared_ptr<request_body_source> source)
    {
        body_.clear();
        body_source_ = std::move(source);
        headers_.erase("Content-Length");
        if (body_source_ && body_source_->content_length())
            headers_["Content-Length"] =
                std::to_string(*body_source_->content_length());
        return *this;
    }

    auto& set_body_stream(request_body_source source)
    {
        return set_body_stream(
            std::make_shared<request_body_source>(std::move(source)));
    }

    auto& set_body_source(std::shared_ptr<request_body_source> source)
    {
        return set_body_stream(std::move(source));
    }

    // --- Access ---

    [[nodiscard]] auto method() const noexcept -> http_method
    {
        return method_;
    }

    [[nodiscard]] auto uri() const noexcept -> std::string_view
    {
        return uri_;
    }

    [[nodiscard]] auto version() const noexcept -> http_version
    {
        return version_;
    }

    [[nodiscard]] auto headers() const noexcept -> const header_map&
    {
        return headers_;
    }

    [[nodiscard]] auto body() const noexcept -> std::string_view
    {
        return body_;
    }

    [[nodiscard]] auto body_source() const noexcept
        -> const std::shared_ptr<request_body_source>&
    {
        return body_source_;
    }

    [[nodiscard]] auto has_streaming_body() const noexcept -> bool
    {
        return static_cast<bool>(body_source_);
    }

    [[nodiscard]] auto get_header(std::string_view key) const -> std::string_view
    {
        auto it = headers_.find(key);
        if (it != headers_.end())
            return it->second;
        return {};
    }

    // --- Serialization ---

    /// Serialize to complete HTTP request string
    [[nodiscard]] auto serialize() const -> std::string
    {
        std::string out;
        out.reserve(256 + body_.size());

        // Request line: "GET /path HTTP/1.1\r\n"
        out += method_to_string(method_);
        out += ' ';
        out += uri_;
        out += ' ';
        out += version_to_string(version_);
        out += "\r\n";

        // Headers
        for (auto& [k, v] : headers_)
        {
            out += k;
            out += ": ";
            out += v;
            out += "\r\n";
        }

        // Empty line
        out += "\r\n";

        // Body
        if (!body_.empty())
        {
            out += body_;
        }

        return out;
    }

private:
    http_method method_ = http_method::GET;
    std::string uri_ = "/";
    http_version version_ = http_version::http_1_1;
    header_map headers_;
    std::string body_;
    std::shared_ptr<request_body_source> body_source_;
};

} // namespace cnetmod::http
