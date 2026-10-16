module;
#include <cnetmod/config.hpp>
export module cnetmod.protocol.http:parser;
import std;
import cnetmod.protocol.http.semantics;

namespace cnetmod::http {
namespace detail {
    auto find_crlf(const char* data, std::size_t size) noexcept -> std::size_t;
    auto trim(std::string_view value) noexcept -> std::string_view;
} // namespace detail

export class request_parser
{
public:
    request_parser();
    [[nodiscard]] auto consume(const char*, std::size_t)
        -> std::expected<std::size_t, std::error_code>;
    [[nodiscard]] auto ready() const noexcept -> bool;
    [[nodiscard]] auto headers_ready() const noexcept -> bool;
    [[nodiscard]] auto method() const noexcept -> std::string_view;
    [[nodiscard]] auto method_enum() const noexcept -> std::optional<http_method>;
    [[nodiscard]] auto uri() const noexcept -> std::string_view;
    [[nodiscard]] auto version() const noexcept -> http_version;
    [[nodiscard]] auto headers() const -> const header_map&;
    [[nodiscard]] auto body() const noexcept -> std::string_view;
    [[nodiscard]] auto take_body_chunk() -> std::string;
    [[nodiscard]] auto get_header(std::string_view) const -> std::string_view;
    /// Reset message state and retain bytes already received after the current
    /// message. Returns true when another message can be parsed immediately.
    [[nodiscard]] auto reset_for_next_message() noexcept -> bool;
    /// Parse retained bytes after reset_for_next_message() without appending
    /// another receive buffer.
    [[nodiscard]] auto consume_buffered()
        -> std::expected<void, std::error_code>;
    [[nodiscard]] auto has_buffered_data() const noexcept -> bool;
    void reset() noexcept;

private:
    enum class state
    {
        request_line,
        headers,
        body
    };

    auto parse_request_line(std::string_view)
        -> std::expected<void, std::error_code>;
    auto parse_header_line(std::string_view)
        -> std::expected<void, std::error_code>;
    auto prepare_body() -> bool;
    auto process_chunked_body() -> std::expected<bool, std::error_code>;
    void finalize_headers();
    void materialize_headers() const;
    [[nodiscard]] auto readable_buffer() const noexcept -> std::string_view;
    [[nodiscard]] auto header_name(std::size_t index) const noexcept
        -> std::string_view;
    [[nodiscard]] auto header_value(std::size_t index) const noexcept
        -> std::string_view;
    [[nodiscard]] auto find_header(std::string_view name) const noexcept
        -> std::size_t;
    void compact_buffer();
    void reset_message_state() noexcept;

    struct header_field
    {
        std::size_t name_offset{};
        std::size_t name_size{};
        std::size_t value_offset{};
        std::size_t value_size{};
        bool duplicate{};
    };

    static constexpr std::size_t common_header_count = 17;
    static constexpr std::size_t missing_header =
        std::numeric_limits<std::size_t>::max();

    std::string buf_, body_;
    std::string header_storage_;
    std::size_t method_offset_{}, method_size_{}, uri_offset_{}, uri_size_{};
    http_version version_ = http_version::http_1_1;
    std::vector<header_field> header_fields_;
    std::vector<std::size_t> header_index_;
    std::array<std::size_t, common_header_count> common_headers_{};
    mutable header_map headers_;
    mutable bool headers_materialized_ = false;
    state state_ = state::request_line;
    std::size_t read_pos_ = 0, header_bytes_ = 0, body_bytes_remaining_ = 0,
                body_bytes_received_ = 0;
    bool chunked_ = false, headers_ready_ = false, ready_ = false;
};

export class response_parser
{
public:
    response_parser() = default;
    [[nodiscard]] auto consume(const char*, std::size_t)
        -> std::expected<std::size_t, std::error_code>;
    [[nodiscard]] auto ready() const noexcept -> bool;
    [[nodiscard]] auto version() const noexcept -> http_version;
    [[nodiscard]] auto status_code() const noexcept -> int;
    [[nodiscard]] auto status_message() const noexcept -> std::string_view;
    [[nodiscard]] auto headers() const noexcept -> const header_map&;
    [[nodiscard]] auto body() const noexcept -> std::string_view;
    [[nodiscard]] auto get_header(std::string_view) const -> std::string_view;
    void reset() noexcept;

private:
    enum class state
    {
        status_line,
        headers,
        body
    };

    auto parse_status_line(std::string_view)
        -> std::expected<void, std::error_code>;
    auto parse_header_line(std::string_view)
        -> std::expected<void, std::error_code>;
    auto prepare_body() -> bool;
    auto process_chunked_body() -> std::expected<bool, std::error_code>;
    [[nodiscard]] auto readable_buffer() const noexcept -> std::string_view;
    void compact_buffer();
    std::string buf_, status_msg_, body_;
    http_version version_ = http_version::http_1_1;
    int status_code_ = 0;
    header_map headers_;
    state state_ = state::status_line;
    std::size_t read_pos_ = 0, header_bytes_ = 0, body_bytes_remaining_ = 0;
    bool chunked_ = false, ready_ = false;
};
} // namespace cnetmod::http
