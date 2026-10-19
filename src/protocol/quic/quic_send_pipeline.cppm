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

/// Packet-owner-only FIFO for encoded, intentionally unreliable DATAGRAMs.
/// Capacity rejection drops the new frame without changing queued ownership.
export class bounded_datagram_queue
{
public:
    static constexpr std::size_t max_frames{128U};
    static constexpr std::size_t max_capacity_bytes{256U * 1024U};

    bounded_datagram_queue();
    ~bounded_datagram_queue();
    bounded_datagram_queue(const bounded_datagram_queue&) = delete;
    auto operator=(const bounded_datagram_queue&) -> bounded_datagram_queue& = delete;

    [[nodiscard]] auto try_push(std::vector<std::byte>&& frame) -> bool;
    [[nodiscard]] auto empty() const noexcept -> bool;
    [[nodiscard]] auto size() const noexcept -> std::size_t;
    [[nodiscard]] auto front() const noexcept -> const std::vector<std::byte>&;
    [[nodiscard]] auto retained_capacity_bytes() const noexcept -> std::size_t;
    auto pop_front() noexcept -> void;
    auto clear() noexcept -> void;

private:
    std::deque<std::vector<std::byte>> frames_;
    std::size_t retained_capacity_bytes_{};
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

    /// STREAM header plus a small owned application prefix and a shared body.
    /// Shared strategies expose no bytes through the owned-only projection.
    [[nodiscard]] static auto shared_stream_with_prefix(
        std::span<const std::byte> header, std::span<const std::byte> prefix,
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
    auto set_priority(std::uint8_t urgency, bool incremental) noexcept -> void;
    [[nodiscard]] auto urgency() const noexcept -> std::uint8_t;
    [[nodiscard]] auto incremental() const noexcept -> bool;
    auto mark_early_data() noexcept -> void;
    [[nodiscard]] auto is_early_data() const noexcept -> bool;

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
    std::uint8_t urgency_{3U};
    bool incremental_{};
    bool early_data_{};
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
    // Standard STREAM codec: count uses QUIC LEN varint; FIN does not change width.
    using shared_stream_header_writer = std::size_t (*)(std::span<std::byte>,
        std::uint64_t, std::uint64_t, std::size_t, bool);
    auto enqueue_shared_write(std::uint64_t stream, std::vector<std::byte> prefix,
        std::shared_ptr<const std::string> body, std::uint64_t first_offset,
        bool fin, std::size_t legacy_budget, shared_stream_header_writer writer) -> void;
    [[nodiscard]] auto has_pending_writes() const noexcept -> bool;
    [[nodiscard]] auto pending_progress() const noexcept -> std::uint64_t;
    auto begin_packet_selection() -> void;
    [[nodiscard]] auto take_selected_frame(std::size_t frame_budget,
        std::optional<std::size_t> pending_credit_budget) -> std::optional<scheduled_quic_frame>;
    auto rollback_selected_frame(scheduled_quic_frame frame) -> void;
    auto finish_packet_selection(bool committed) -> void;
    [[nodiscard]] auto take() -> std::optional<scheduled_quic_frame>;
    auto requeue(scheduled_quic_frame frame) -> void;
    [[nodiscard]] auto requeue_retransmission(quic_send_frame& frame) -> bool;
    auto set_priority(std::uint64_t stream, std::uint8_t urgency,
        bool incremental) noexcept -> void;
    /// Forget application metadata; queued/recovery frames own their priority.
    auto retire_stream(std::uint64_t stream) noexcept -> void;
    auto discard_early_data() noexcept -> void;
    [[nodiscard]] auto take_early_data() -> std::vector<quic_send_frame>;
    [[nodiscard]] auto priority_count() const noexcept -> std::size_t;

private:
    class implementation;
    std::unique_ptr<implementation> impl_;
};

} // namespace cnetmod::quic
