module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.http:response;

import std;
import :types;

namespace cnetmod::http {

// =============================================================================
// response — HTTP Response Builder
// =============================================================================

export class response {
public:
    response() = default;

    explicit response(int status_code,
                      http_version version = http_version::http_1_1)
        : status_code_(status_code), version_(version) {}

    // --- Settings ---

    auto& set_status(int code) noexcept { status_code_ = code; return *this; }
    auto& set_status_message(std::string_view msg) { status_msg_ = std::string(msg); return *this; }
    auto& set_version(http_version v) noexcept { version_ = v; return *this; }

    auto& set_header(std::string_view key, std::string_view value) {
        headers_[std::string(key)] = std::string(value);
        return *this;
    }

    auto& append_header(std::string_view key, std::string_view value) {
        auto it = headers_.find(std::string(key));
        if (it != headers_.end()) {
            it->second += ", ";
            it->second += value;
        } else {
            headers_[std::string(key)] = std::string(value);
        }
        return *this;
    }

    auto& remove_header(std::string_view key) {
        headers_.erase(std::string(key));
        return *this;
    }

    auto& set_body(std::string_view body) {
        body_ = std::string(body);
        headers_["Content-Length"] = std::to_string(body_.size());
        return *this;
    }

    auto& set_body(std::string body) {
        headers_["Content-Length"] = std::to_string(body.size());
        body_ = std::move(body);
        return *this;
    }

    // --- Access ---

    [[nodiscard]] auto status_code() const noexcept -> int { return status_code_; }
    [[nodiscard]] auto version() const noexcept -> http_version { return version_; }
    [[nodiscard]] auto headers() const noexcept -> const header_map& { return headers_; }
    [[nodiscard]] auto body() const noexcept -> std::string_view { return body_; }

    [[nodiscard]] auto get_header(std::string_view key) const -> std::string_view {
        auto it = headers_.find(std::string(key));
        if (it != headers_.end()) return it->second;
        return {};
    }

    // --- Serialization ---

    /// Serialize to complete HTTP response string
    [[nodiscard]] auto serialize() const -> std::string {
        std::string out;
        out.reserve(256 + body_.size());

        // Status line: "HTTP/1.1 200 OK\r\n"
        out += version_to_string(version_);
        out += ' ';
        out += std::to_string(status_code_);
        out += ' ';
        if (!status_msg_.empty()) {
            out += status_msg_;
        } else {
            out += status_reason(status_code_);
        }
        out += "\r\n";

        // Headers
        for (auto& [k, v] : headers_) {
            out += k;
            out += ": ";
            out += v;
            out += "\r\n";
        }

        // Empty line
        out += "\r\n";

        // Body
        if (!body_.empty()) {
            out += body_;
        }

        return out;
    }

private:
    int status_code_ = 200;
    std::string status_msg_;
    http_version version_ = http_version::http_1_1;
    header_map headers_;
    std::string body_;
};

} // namespace cnetmod::http
