module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_HAS_SSL

    #ifdef CNETMOD_ENABLE_QUIC

export module cnetmod.protocol.quic:stream;

import std;

import cnetmod.core.buffer;
import cnetmod.coro.task;
import cnetmod.coro.channel;
import :types;

namespace cnetmod::quic {

// =============================================================================
// Stream States (RFC 9000 §3)
// =============================================================================

export enum class stream_state
{
    idle,               // No activity
    open,               // Bidirectional open
    half_closed_local,  // Local finished sending
    half_closed_remote, // Remote finished sending
    closed              // Closed completely
};

// =============================================================================
// QUIC Stream Interface
// =============================================================================

export class quic_stream
{
public:
    /// Constructor
    explicit quic_stream(
        stream_id id,
        quic_role owner,
        bool bidirectional);

    ~quic_stream();

    /// Cannot copy/move
    quic_stream(const quic_stream&) = delete;
    quic_stream& operator=(const quic_stream&) = delete;

    /// Initialize stream state
    auto init() -> void;

    // =========================================================================
    // Send Operations
    // =========================================================================

    /// Send data on stream (non-blocking, queues if blocked)
    [[nodiscard]] auto send(std::span<const std::byte> data)
        -> task<std::expected<void, std::error_code>>;

    /// Close local send direction (fin flag)
    [[nodiscard]] auto close_local() -> task<void>;

    /// Close both directions
    [[nodiscard]] auto close_both() -> task<void>;

    // =========================================================================
    // Receive Operations
    // =========================================================================

    /// Receive data into buffer (non-blocking, suspends if no data)
    [[nodiscard]] auto receive(mutable_buffer buf)
        -> task<std::expected<std::size_t, std::error_code>>;

    /// Receive until delimiter found
    [[nodiscard]] auto receive_until_delimiter(
        char delim,
        dynamic_buffer& out)
        -> task<std::expected<std::size_t, std::error_code>>;

    /// Deliver authenticated STREAM data from the connection.  Fragments may
    /// arrive out of order; duplicates are ignored and FIN closes the remote
    /// direction once the final range becomes contiguous.
    auto push_received(std::uint64_t offset, std::span<const std::byte> data,
        bool fin) -> std::expected<void, std::error_code>;
    auto reset_remote(std::uint64_t final_size) -> std::expected<void, std::error_code>;
    void stop_local() noexcept;

    /// Set peer-advertised send credit.  This is independent from the local
    /// receive window used by push_received().
    void update_send_limit(std::uint64_t maximum) noexcept;
    void set_initial_receive_limit(std::uint64_t maximum) noexcept;
    void extend_receive_limit(std::uint64_t maximum) noexcept;

    /// Close remote receive direction
    auto close_remote() -> void;

    // =========================================================================
    // Query Methods
    // =========================================================================

    /// Get current stream state
    [[nodiscard]] auto state() const noexcept -> stream_state;

    /// Check if readable (has received data)
    [[nodiscard]] auto is_readable() const noexcept -> bool;

    /// Check if writable (can send without blocking)
    [[nodiscard]] auto is_writable() const noexcept -> bool;

    /// Get remaining receive window
    [[nodiscard]] auto remaining_receive_window() const noexcept -> std::uint64_t;

    /// Total bytes received
    [[nodiscard]] auto bytes_received() const noexcept -> std::uint64_t;
    [[nodiscard]] auto bytes_consumed() const noexcept -> std::uint64_t;

    /// Total bytes sent
    [[nodiscard]] auto bytes_sent() const noexcept -> std::uint64_t;

    /// Stream ID
    [[nodiscard]] auto id() const noexcept -> stream_id;

    /// Is bidirectional?
    [[nodiscard]] auto is_bidirectional() const noexcept -> bool;

private:
    struct impl
    {
        stream_id id_;
        quic_role owner_;
        bool bidirectional_{true};

        // State tracking
        stream_state state_ = stream_state::idle;

        // Receive buffer (sorted by offset for gaps)
        std::map<std::uint64_t, std::vector<std::byte>> receive_buffer_;
        std::uint64_t next_expected_offset_ = 0;
        std::uint64_t highest_received_offset_ = 0;
        std::optional<std::uint64_t> final_size_;

        // Send queue
        std::deque<std::pair<std::uint64_t, std::vector<std::byte>>> send_queue_;
        std::uint64_t total_sent_ = 0;

        // Flow control limits
        std::uint64_t max_data_ = 65536;
        std::uint64_t max_send_data_ = 262144;

        // Notification channel for reads
        channel<std::vector<std::byte>> notify_channel_;
    };

    std::unique_ptr<impl> impl_;

    /// Deliver contiguous data from beginning of receive buffer
    auto deliver_contiguous_data(mutable_buffer buf) -> std::size_t;

    /// Drain receive buffer after receiving MAX_STREAM_DATA
    auto drain_receive_buffer() -> task<void>;
};

} // namespace cnetmod::quic

    #endif // CNETMOD_ENABLE_QUIC
#endif     // CNETMOD_HAS_SSL
