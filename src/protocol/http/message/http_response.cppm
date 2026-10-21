module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.http:response;
// Protocol-qualified filename prevents duplicate source basenames.

import std;
import cnetmod.protocol.http.semantics;
import :cookie;

namespace cnetmod::http {

// =============================================================================
// response — HTTP Response Builder
// =============================================================================

export class response
{
public:
    response();

    explicit response(int status_code,
        http_version version = http_version::http_1_1);

    // --- Settings ---

    auto set_status(int code) noexcept -> response&;
    auto set_status_message(std::string_view msg) -> response&;
    auto set_version(http_version v) noexcept -> response&;

    auto set_header(std::string_view key, std::string_view value) -> response&;

    /// Set a header from an already-owned value. This avoids a second
    /// allocation when a server-side cache has just produced the value.
    auto set_header_owned(std::string_view key, std::string value) -> response&;

    auto append_header(std::string_view key, std::string_view value)
        -> response&;

    auto set_trailer(std::string_view key, std::string_view value) -> response&;

    auto append_trailer(std::string_view key, std::string_view value)
        -> response&;

    auto remove_header(std::string_view key) -> response&;

    auto set_body(std::string_view body) -> response&;

    auto set_body(std::string body) -> response&;

    /// Reuse immutable buffered content without copying it into every
    /// response. The storage remains alive until the transport consumes it.
    auto set_shared_body(std::shared_ptr<const std::string> body) -> response&;

    auto set_body_preserve_headers(std::string body) -> response&;

    /// Install a pull-based response body. The transport consumes the source
    /// with back-pressure after the handler returns; this keeps file and
    /// generated streaming responses protocol-neutral across HTTP/1, HTTP/2,
    /// and TLS.
    auto set_body_source(std::shared_ptr<response_body_source> source)
        -> response&;

    /// Reset this builder while retaining allocated storage for connection-local
    /// response reuse.
    void reset(int status_code = 200,
        http_version version = http_version::http_1_1) noexcept;

    /// Set cookie (simplified interface)
    auto set_cookie(std::string_view name, std::string_view value,
        std::string_view domain = {}, std::string_view path = "/",
        std::optional<std::chrono::seconds> max_age = std::nullopt,
        bool secure = false, bool http_only = false) -> response&;

    /// Set cookie (full control)
    auto set_cookie(const cookie& c) -> response&;

    // --- Access ---

    [[nodiscard]] auto status_code() const noexcept -> int;
    [[nodiscard]] auto version() const noexcept -> http_version;
    [[nodiscard]] auto headers() const -> const header_map&;
    [[nodiscard]] auto trailers() const noexcept -> const header_map&;
    [[nodiscard]] auto body() const noexcept -> std::string_view;

    [[nodiscard]] auto take_headers() -> header_map;

    /// Move every response header directly into a transport-owned
    /// representation without first materializing the compatibility
    /// `header_map` snapshot. Transports should prefer this overload on their
    /// hot path; `headers()` and the no-argument `take_headers()` remain the
    /// associative-container API for applications.
    template <typename Visitor>
    void take_headers(Visitor&& visitor)
    {
        if (has_cached_date_header_)
            std::invoke(visitor, std::string{"Date"},
                std::string{cached_date_header_.data(),
                    cached_date_header_.size()});
        auto active = active_common_headers_;
        while (active != 0)
        {
            const auto slot = static_cast<std::size_t>(std::countr_zero(active));
            auto& header = common_headers_[slot];
            std::invoke(visitor,
                std::string{common_header_name(slot)},
                std::move(header.value));
            active &= static_cast<std::uint16_t>(active - 1U);
        }
        for (auto& [name, value] : headers_)
            std::invoke(visitor, std::move(name), std::move(value));

        headers_.clear();
        active = active_common_headers_;
        while (active != 0)
        {
            const auto slot = static_cast<std::size_t>(std::countr_zero(active));
            auto& header = common_headers_[slot];
            header.value.clear();
            header.present = false;
            active &= static_cast<std::uint16_t>(active - 1U);
        }
        active_common_headers_ = 0;
        headers_snapshot_.clear();
        headers_snapshot_valid_ = false;
        has_cached_date_header_ = false;
    }

    [[nodiscard]] auto header_count() const noexcept -> std::size_t;
    [[nodiscard]] auto take_trailers() noexcept -> header_map;
    [[nodiscard]] auto take_body() noexcept -> std::string;
    [[nodiscard]] auto shared_body() const noexcept
        -> const std::shared_ptr<const std::string>&;
    [[nodiscard]] auto take_shared_body() noexcept
        -> std::shared_ptr<const std::string>;
    [[nodiscard]] auto body_source() const noexcept
        -> const std::shared_ptr<response_body_source>&;
    [[nodiscard]] auto take_body_source() noexcept
        -> std::shared_ptr<response_body_source>;

    [[nodiscard]] auto get_header(std::string_view key) const -> std::string_view;

    // --- Serialization ---

    /// Serialize to complete HTTP response string
    [[nodiscard]] auto serialize() const -> std::string;

    /// Serialize into caller-owned storage. Reusing the storage avoids a
    /// transient allocation for every response on a keep-alive connection.
    void serialize_to(std::string& output) const;

    /// Append a complete response to existing caller-owned storage. This is
    /// used by HTTP/1 pipelining to submit several ordered responses with one
    /// kernel write while keeping serialize_to()'s replacement semantics.
    void append_serialized_to(std::string& output) const;

    /// Install a fixed-width, server-generated IMF-fixdate without allocating
    /// a header-map value. A later explicit `set_header("Date", ...)` retains
    /// the normal response-builder override semantics.
    void set_cached_date_header(std::array<char, 29U> value) noexcept;

    [[nodiscard]] auto cached_date_header() const noexcept -> std::string_view;

private:
    static constexpr std::size_t common_header_count = 10;

    struct common_header_value
    {
        std::string value;
        bool present{};
    };

    [[nodiscard]] static auto common_header_slot(std::string_view key) noexcept
        -> std::optional<std::size_t>;
    [[nodiscard]] static auto common_header_name(std::size_t slot) noexcept
        -> std::string_view;
    void invalidate_headers_snapshot() noexcept;
    void materialize_headers() const;
    auto set_common_header(std::size_t slot, std::string_view value)
        -> response&;
    auto set_common_header_owned(std::size_t slot, std::string value)
        -> response&;

    int status_code_ = 200;
    std::string status_msg_;
    http_version version_ = http_version::http_1_1;
    header_map headers_;
    std::array<common_header_value, common_header_count> common_headers_{};
    std::uint16_t active_common_headers_{};
    mutable header_map headers_snapshot_;
    mutable bool headers_snapshot_valid_{};
    header_map trailers_;
    std::string body_;
    std::shared_ptr<const std::string> shared_body_;
    std::shared_ptr<response_body_source> body_source_;
    std::array<char, 29U> cached_date_header_{};
    bool has_cached_date_header_ = false;
};

} // namespace cnetmod::http
