export module cnetmod.protocol.http.v2.stream;

import std;
import cnetmod.protocol.http.v2.flow_control;
import cnetmod.protocol.http.v2.header_compression;
import cnetmod.protocol.http.semantics;

export namespace cnetmod::http::v2 {
enum class stream_state
{
    idle,
    open,
    half_closed_local,
    half_closed_remote,
    closed
};

class stream
{
public:
    explicit stream(std::uint32_t id,
        std::int32_t initial_window = 65'535) noexcept;
    stream(std::uint32_t id, std::int32_t initial_window,
        std::vector<header_field> reusable_headers) noexcept;
    stream(std::uint32_t id, std::int32_t send_window, std::int32_t receive_window,
        std::vector<header_field> reusable_headers) noexcept;
    [[nodiscard]] auto id() const noexcept -> std::uint32_t;
    [[nodiscard]] auto state() const noexcept -> stream_state;
    [[nodiscard]] auto receive_headers(std::vector<header_field> fields,
        bool end_stream) -> bool;
    [[nodiscard]] auto prepare_headers(bool end_stream) noexcept
        -> std::vector<header_field>*;
    [[nodiscard]] auto receive_data(std::span<const std::byte> data,
        bool end_stream) -> bool;
    [[nodiscard]] auto headers() const noexcept
        -> const std::vector<header_field>&;
    [[nodiscard]] auto take_headers() noexcept
        -> std::vector<header_field>;
    [[nodiscard]] auto body() const noexcept -> std::span<const std::byte>;
    [[nodiscard]] auto take_body() noexcept -> std::vector<std::byte>;
    void attach_body_stream(
        std::shared_ptr<cnetmod::http::request_body_stream> body_stream);
    [[nodiscard]] auto body_stream() const noexcept
        -> const std::shared_ptr<cnetmod::http::request_body_stream>&;
    [[nodiscard]] auto receive_window() noexcept -> flow_window&;
    [[nodiscard]] auto send_window() noexcept -> flow_window&;

private:
    std::uint32_t id_{};
    stream_state state_ = stream_state::idle;
    flow_window receive_window_;
    flow_window send_window_;
    std::vector<header_field> headers_;
    std::vector<std::byte> body_;
    std::shared_ptr<cnetmod::http::request_body_stream> body_stream_;
};
} // namespace cnetmod::http::v2
