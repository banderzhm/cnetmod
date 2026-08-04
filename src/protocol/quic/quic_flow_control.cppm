module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.quic:flow_control;

import std;

namespace cnetmod::quic {

// =============================================================================
// Connection-Level Flow Controller (RFC 9002 §7)
// =============================================================================

export class flow_controller
{
public:
    explicit flow_controller(std::uint64_t initial_max_data = 1048576);

    /// Consuming from send/receive windows
    auto consume_send(std::uint64_t bytes) -> std::expected<bool, std::error_code>; // returns true if should send update
    auto consume_recv(std::uint64_t bytes) -> std::expected<bool, std::error_code>; // returns true if should send update

    /// Window queries
    auto remaining_send_window() const noexcept -> std::uint64_t;
    auto remaining_recv_window() const noexcept -> std::uint64_t;
    auto total_sent() const noexcept -> std::uint64_t;
    auto total_received() const noexcept -> std::uint64_t;

    /// Max limits
    auto max_data() const noexcept -> std::uint64_t;
    void increase_max_data(std::uint64_t delta) noexcept;

    /// Should we send MAX_DATA update?
    auto should_send_max_data_update() const noexcept -> bool;

private:
    std::uint64_t max_data_;
    std::uint64_t current_limit_ = 1048576; // 1MB initial
    std::uint64_t current_total_offset_ = 0;

    /// Update threshold: 50% of window used
    inline static constexpr double update_threshold_percent = 0.5;
};

// =============================================================================
// Stream-Level Flow Controller
// =============================================================================

export class stream_flow_controller
{
public:
    explicit stream_flow_controller(std::uint64_t initial_max_stream_data = 262144);

    auto consume_send(std::uint64_t bytes) -> std::expected<bool, std::error_code>;
    auto consume_recv(std::uint64_t bytes) -> std::expected<bool, std::error_code>;

    auto remaining_send_window() const noexcept -> std::uint64_t;
    auto remaining_recv_window() const noexcept -> std::uint64_t;
    auto total_received() const noexcept -> std::uint64_t;

    auto should_send_max_stream_data_update() const noexcept -> bool;
    auto stream_max_data() const noexcept -> std::uint64_t;
    void increase_stream_max_data(std::uint64_t delta) noexcept;

private:
    std::uint64_t max_stream_data_;
    std::uint64_t current_stream_limit_ = 262144;
    std::uint64_t stream_total_offset_ = 0;
};

// =============================================================================
// Implementation - Connection Level
// =============================================================================

flow_controller::flow_controller(std::uint64_t initial_max_data)
    : max_data_(initial_max_data),
      current_limit_(initial_max_data),
      current_total_offset_(0)
{
}

auto flow_controller::consume_send(std::uint64_t bytes)
    -> std::expected<bool, std::error_code>
{
    // Check if we have enough room in our send window
    if (bytes > remaining_send_window())
    {
        return std::unexpected(std::make_error_code(
            std::errc::not_supported)); // Could be a custom error code for flow control
    }

    current_total_offset_ += bytes;

    // Check if we should send a MAX_DATA update
    // Spec says trigger when offset exceeds new limit by more than max_data / 2
    const auto half_max = max_data_ / 2;
    const bool should_update = half_max != 0U && current_total_offset_ >= half_max &&
        current_total_offset_ % half_max == 0U;

    return should_update;
}

auto flow_controller::consume_recv(std::uint64_t bytes)
    -> std::expected<bool, std::error_code>
{
    if (bytes > remaining_recv_window())
    {
        return std::unexpected(std::make_error_code(
            std::errc::no_buffer_space)); // Would need custom error codes
    }

    current_limit_ -= bytes;

    // Check if we should send a MAX_DATA update
    const auto update_threshold = static_cast<double>(current_limit_) * 0.5;
    const bool should_update = (update_threshold <= max_data_ - current_limit_);

    return should_update;
}

auto flow_controller::remaining_send_window() const noexcept -> std::uint64_t
{
    return max_data_ - current_total_offset_;
}

auto flow_controller::remaining_recv_window() const noexcept -> std::uint64_t
{
    return current_limit_;
}

auto flow_controller::total_sent() const noexcept -> std::uint64_t
{
    return current_total_offset_;
}

auto flow_controller::total_received() const noexcept -> std::uint64_t
{
    return max_data_ - current_limit_;
}

auto flow_controller::max_data() const noexcept -> std::uint64_t
{
    return max_data_;
}

void flow_controller::increase_max_data(std::uint64_t delta) noexcept
{
    max_data_ += delta;
    current_limit_ += delta;
}

auto flow_controller::should_send_max_data_update() const noexcept -> bool
{
    // Trigger when we've consumed more than half the window
    const auto consumed = max_data_ - current_limit_;
    const auto threshold = static_cast<double>(max_data_) * update_threshold_percent;
    return consumed >= threshold && (max_data_ - current_limit_) > 0;
}

// =============================================================================
// Implementation - Stream Level
// =============================================================================

stream_flow_controller::stream_flow_controller(std::uint64_t initial_max_stream_data)
    : max_stream_data_(initial_max_stream_data),
      current_stream_limit_(initial_max_stream_data),
      stream_total_offset_(0)
{
}

auto stream_flow_controller::consume_send(std::uint64_t bytes)
    -> std::expected<bool, std::error_code>
{
    if (bytes > remaining_send_window())
    {
        return std::unexpected(std::make_error_code(
            std::errc::not_supported));
    }

    stream_total_offset_ += bytes;

    // Check if should send MAX_STREAM_DATA
    const auto half_stream = max_stream_data_ / 2;
    const bool should_update = (stream_total_offset_ >= half_stream &&
        stream_total_offset_ % half_stream == 0);

    return should_update;
}

auto stream_flow_controller::consume_recv(std::uint64_t bytes)
    -> std::expected<bool, std::error_code>
{
    if (bytes > remaining_recv_window())
    {
        return std::unexpected(std::make_error_code(
            std::errc::no_buffer_space));
    }

    current_stream_limit_ -= bytes;

    const bool should_update = should_send_max_stream_data_update();
    return should_update;
}

auto stream_flow_controller::remaining_send_window() const noexcept -> std::uint64_t
{
    return max_stream_data_ - stream_total_offset_;
}

auto stream_flow_controller::remaining_recv_window() const noexcept -> std::uint64_t
{
    return current_stream_limit_;
}

auto stream_flow_controller::total_received() const noexcept -> std::uint64_t
{
    return max_stream_data_ - current_stream_limit_;
}

auto stream_flow_controller::should_send_max_stream_data_update() const noexcept -> bool
{
    const auto consumed = max_stream_data_ - current_stream_limit_;
    const auto threshold = static_cast<double>(max_stream_data_) * 0.5;
    return consumed >= threshold && (max_stream_data_ - current_stream_limit_) > 0;
}

auto stream_flow_controller::stream_max_data() const noexcept -> std::uint64_t
{
    return max_stream_data_;
}

void stream_flow_controller::increase_stream_max_data(std::uint64_t delta) noexcept
{
    max_stream_data_ += delta;
    current_stream_limit_ += delta;
}

} // namespace cnetmod::quic
