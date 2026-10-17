module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.quic.send_pipeline;

import std;

namespace cnetmod::quic {

export class priority_service_budget
{
public:
    static constexpr std::array<std::uint8_t, 8> quantum{
        16U, 8U, 4U, 2U, 1U, 1U, 1U, 1U};

    [[nodiscard]] auto take_next(const std::array<bool, 8>& ready)
        -> std::optional<std::uint8_t>;
    void restore(std::uint8_t urgency) noexcept;
    void reset() noexcept;

private:
    std::array<std::uint8_t, 8> credits_{quantum};
};

/// A retransmittable application frame. Static response data uses a shared
/// slice strategy, so packet recovery retains offsets rather than body copies.
export class quic_send_frame
{
public:
    quic_send_frame() = default;
    quic_send_frame(std::vector<std::byte> bytes);

    [[nodiscard]] static auto owned(std::vector<std::byte> bytes,
        std::optional<std::uint64_t> stream = std::nullopt) -> quic_send_frame;
    [[nodiscard]] static auto shared_stream(std::span<const std::byte> header,
        std::shared_ptr<const std::string> body, std::size_t body_offset,
        std::size_t body_size, std::uint64_t stream) -> quic_send_frame;

    [[nodiscard]] auto empty() const noexcept -> bool;
    [[nodiscard]] auto size() const noexcept -> std::size_t;
    [[nodiscard]] auto stream() const noexcept -> std::optional<std::uint64_t>;
    [[nodiscard]] auto is_connection_close() const noexcept -> bool;
    [[nodiscard]] auto owned_bytes() const noexcept -> const std::vector<std::byte>&;
    [[nodiscard]] auto release_owned_bytes() noexcept -> std::vector<std::byte>;
    auto append_to(std::vector<std::byte>& output) const -> void;
    auto clear() noexcept -> void;

    friend auto operator==(const quic_send_frame& lhs,
        const quic_send_frame& rhs) noexcept -> bool;

private:
    std::vector<std::byte> owned_bytes_;
    std::shared_ptr<const std::string> shared_bytes_;
    std::array<std::byte, 32> stream_header_{};
    std::size_t stream_header_size_{};
    std::size_t shared_offset_{};
    std::size_t shared_size_{};
    std::optional<std::uint64_t> stream_;
};

export struct scheduled_quic_frame
{
    std::uint64_t stream{};
    std::uint8_t urgency{};
    bool default_fifo{};
    quic_send_frame frame;
};

/// Strategy-aware RFC 9218 scheduler owned by one connection serial domain.
export class quic_stream_scheduler
{
public:
    quic_stream_scheduler();
    ~quic_stream_scheduler();
    quic_stream_scheduler(quic_stream_scheduler&&) noexcept;
    auto operator=(quic_stream_scheduler&&) noexcept -> quic_stream_scheduler&;
    quic_stream_scheduler(const quic_stream_scheduler&) = delete;
    auto operator=(const quic_stream_scheduler&) -> quic_stream_scheduler& = delete;

    [[nodiscard]] auto empty() const noexcept -> bool;
    [[nodiscard]] auto size() const noexcept -> std::size_t;
    auto enqueue(std::uint64_t stream, quic_send_frame frame) -> void;
    [[nodiscard]] auto take() -> std::optional<scheduled_quic_frame>;
    auto requeue(scheduled_quic_frame frame) -> void;
    [[nodiscard]] auto requeue_retransmission(quic_send_frame& frame) -> bool;
    auto set_priority(std::uint64_t stream, std::uint8_t urgency,
        bool incremental) noexcept -> void;

private:
    class implementation;
    std::unique_ptr<implementation> impl_;
};

} // namespace cnetmod::quic
