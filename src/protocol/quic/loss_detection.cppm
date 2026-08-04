module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.quic:loss_detection;

import std;
import :types;
import :frame;

namespace cnetmod::quic {

// =============================================================================
// RTT Sample Structure (RFC 9002 §7)
// =============================================================================

export struct rtt_sample
{
    std::chrono::steady_clock::duration smoothed_rtt_{};
    std::chrono::steady_clock::duration rtt_var_{};
    std::chrono::steady_clock::duration min_rtt_{};
    std::chrono::steady_clock::duration latest_rtt_{};
};

// =============================================================================
// PTO Calculation Parameters
// =============================================================================

export struct pto_params
{
    std::uint32_t count = 3; // probe packet count
};

// =============================================================================
// Loss Detection State for a Packet Number Space
// =============================================================================

export class loss_detector
{
public:
    explicit loss_detector(const quic_config& config)
        : config_(config)
    {
        // Min RTT starts at infinity, will be reduced on first sample
        rtt_samples_.min_rtt_ = std::chrono::seconds{60};
    }

    /// Packet sent notification
    void on_packet_sent(
        std::uint64_t pn,
        std::uint64_t bytes_in_flight,
        time_point send_time,
        bool ack_eliciting,
        pn_space space)
    {
        auto& space_state = get_pn_space_state(space);

        // ACK ranges report packet numbers, not merely packets that elicited
        // an ACK. A later ACK-eliciting packet can cause the peer to include
        // intervening ACK-only packets in the same range (RFC 9000 section
        // 13.2). Track every sent packet number for protocol validation while
        // keeping only ACK-eliciting packets in the recovery map.
        space_state.largest_sent_packet_number_ = pn;
        space_state.has_sent_packet_ = true;

        // Only ack-eliciting packets participate in loss detection or
        // bytes-in-flight accounting (RFC 9002 §3).
        if (!ack_eliciting)
            return;

        space_state.in_flight_packets_.emplace(pn, inflight_packet{pn, bytes_in_flight, send_time, ack_eliciting});
        space_state.bytes_in_flight_ += bytes_in_flight;
        space_state.time_of_last_ack_eliciting_packet_ = send_time;
    }

    /// ACK received notification
    auto on_ack_received(
        const ack_frame& frame,
        std::uint64_t largest_acked_pn,
        time_point recv_time,
        pn_space space) -> std::expected<std::vector<std::uint64_t>, std::error_code>
    {
        auto& space_state = get_pn_space_state(space);
        if (!space_state.has_sent_packet_ ||
            largest_acked_pn > space_state.largest_sent_packet_number_)
            return std::unexpected(make_error_code(quic_errc::protocol_violation));
        if (frame.largest_acked != largest_acked_pn)
            return std::unexpected(make_error_code(quic_errc::frame_encoding_error));
        if (space_state.largest_acked_pn_ == std::numeric_limits<std::uint64_t>::max() ||
            largest_acked_pn > space_state.largest_acked_pn_)
            space_state.largest_acked_pn_ = largest_acked_pn;

        // Track largest acknowledged PN and its sent time
        auto it = space_state.in_flight_packets_.find(largest_acked_pn);
        if (it != space_state.in_flight_packets_.end())
        {
            space_state.largest_acked_pn_sent_time_ = it->second.sent_time;

            // Calculate RTT sample
            auto rtt = recv_time - it->second.sent_time;
            update_rtt_estimate(rtt, static_cast<std::uint32_t>(frame.ack_delay));
        }

        // Collect newly acked packets
        std::vector<std::uint64_t> newly_acked;

        // ACK ranges are descending.  first_ack_range describes the
        // contiguous range ending at largest_acked; every subsequent gap is
        // the number of missing packets minus one (RFC 9000 §19.3).
        if (frame.first_ack_range > largest_acked_pn)
            return std::unexpected(make_error_code(quic_errc::frame_encoding_error));
        auto range_high = largest_acked_pn;
        auto range_low = range_high - frame.first_ack_range;
        auto acknowledge_range = [&](std::uint64_t low, std::uint64_t high)
        {
            // ACK ranges are peer-controlled. Iterate only tracked packets,
            // never every number in a potentially attacker-sized range.
            auto it = space_state.in_flight_packets_.lower_bound(low);
            while (it != space_state.in_flight_packets_.end() && it->first <= high)
            {
                newly_acked.push_back(it->first);
                space_state.bytes_in_flight_ -= it->second.bytes;
                it = space_state.in_flight_packets_.erase(it);
            }
        };
        acknowledge_range(range_low, range_high);
        for (const auto& range : frame.ack_ranges)
        {
            if (range_low < range.gap + 2)
                return std::unexpected(make_error_code(quic_errc::frame_encoding_error));
            range_high = range_low - range.gap - 2;
            if (range.ack_range_length > range_high)
                return std::unexpected(make_error_code(quic_errc::frame_encoding_error));
            range_low = range_high - range.ack_range_length;
            acknowledge_range(range_low, range_high);
        }
        // Reset PTO countdown if new data acknowledged
        if (!newly_acked.empty())
        {
            // Any newly acknowledged ack-eliciting packet proves forward
            // progress and resets PTO backoff (RFC 9002 §6.2.1).
            space_state.pto_count_ = 0;
        }

        return newly_acked;
    }

    /// Detect lost packets based on threshold and timers
    auto detect_lost_packets(time_point now, pn_space space) -> std::vector<std::uint64_t>
    {
        auto& space_state = get_pn_space_state(space);
        std::vector<std::uint64_t> lost;

        if (space_state.largest_acked_pn_ == std::numeric_limits<std::uint64_t>::max())
            return lost;
        auto max_rtt = rtt_samples_.smoothed_rtt_;
        if (max_rtt == duration_type{})
            max_rtt = std::chrono::milliseconds(333);
        if (rtt_samples_.latest_rtt_ > max_rtt)
        {
            max_rtt = rtt_samples_.latest_rtt_;
        }

        const auto loss_threshold = std::max(max_rtt + max_rtt / 8,
            duration_type{std::chrono::milliseconds(1)});

        // Time-based loss detection
        for (auto it = space_state.in_flight_packets_.begin();
            it != space_state.in_flight_packets_.end();)
        {
            const bool packet_threshold = it->first + 3 <= space_state.largest_acked_pn_;
            const bool time_threshold = it->second.sent_time + loss_threshold <= now;
            if (it->first <= space_state.largest_acked_pn_ &&
                (packet_threshold || time_threshold))
            {
                lost.push_back(it->first);
                space_state.bytes_in_flight_ -= it->second.bytes;
                it = space_state.in_flight_packets_.erase(it);
            }
            else
                ++it;
        }

        // Threshold-based loss detection
        // Packets with PN <= largest_acked_pn - kPacketThreshold (3) are lost
        return lost;
    }

    /// Get next PTO timeout time (for scheduling probes)
    auto get_loss_time_and_space(pn_space space) -> std::optional<std::pair<time_point, pn_space>>
    {
        auto& space_state = get_pn_space_state(space);

        if (space_state.in_flight_packets_.empty())
        {
            return std::nullopt;
        }

        auto loss_time = get_loss_time_for_space(space, space_state);

        if (loss_time.has_value())
        {
            return std::make_pair(*loss_time, space);
        }

        return std::nullopt;
    }

    /// Earliest PTO deadline across active packet number spaces.  PTO does
    /// not declare packets lost; it asks the transport to send probes.
    auto next_pto_deadline() const -> std::optional<std::pair<time_point, pn_space>>
    {
        std::optional<std::pair<time_point, pn_space>> earliest;
        const auto consider = [&](const pn_space_state& state, pn_space space)
        {
            if (state.in_flight_packets_.empty() ||
                state.time_of_last_ack_eliciting_packet_ == time_point::min())
                return;
            auto timeout = pto_duration_for(space);
            for (std::uint32_t count = 0; count < state.pto_count_; ++count)
                timeout *= 2;
            const auto deadline = state.time_of_last_ack_eliciting_packet_ + timeout;
            if (!earliest || deadline < earliest->first)
                earliest = std::pair{deadline, space};
        };
        consider(initial_space_, pn_space::initial);
        consider(handshake_space_, pn_space::handshake);
        consider(app_space_, pn_space::application);
        return earliest;
    }

    void on_pto_expired(pn_space space) noexcept
    {
        auto& state = get_pn_space_state(space);
        ++state.pto_count_;
    }

    /// Calculate PTO duration
    auto pto_duration() const noexcept -> std::chrono::steady_clock::duration
    {
        // PTO = smoothed_rtt + max(4 * rtt_var, granularity) + max_ack_delay
        const auto smoothed_rtt = rtt_samples_.smoothed_rtt_ == duration_type{}
            ? duration_type{std::chrono::milliseconds(333)}
            : rtt_samples_.smoothed_rtt_;
        const auto rtt_var = rtt_samples_.rtt_var_ == duration_type{}
            ? smoothed_rtt / 2
            : rtt_samples_.rtt_var_;
        auto pto_beyond = std::max<duration_type>(4 * rtt_var, std::chrono::milliseconds(1));

        // Use max ack delay from peer (we don't have this info, use conservative default)
        const auto max_ack_delay = std::chrono::milliseconds(25); // Default from spec

        return smoothed_rtt + pto_beyond + max_ack_delay;
    }

    /// Get current RTT estimates
    auto rtt_estimate() const noexcept -> const struct rtt_sample&
    {
        return rtt_samples_;
    }

    [[nodiscard]] auto in_flight_packet_count(pn_space space) const noexcept
        -> std::size_t
    {
        return get_pn_space_state(space).in_flight_packets_.size();
    }

    [[nodiscard]] auto bytes_in_flight(pn_space space) const noexcept
        -> std::uint64_t
    {
        return get_pn_space_state(space).bytes_in_flight_;
    }

    /// Initialize RTT from first handshake ACK
    void initialize_rtt(std::chrono::steady_clock::duration rtt)
    {
        update_rtt_estimate_internal(rtt);
    }

private:
    using clock_type = std::chrono::steady_clock;
    using duration_type = std::chrono::steady_clock::duration;

    quic_config config_;
    rtt_sample rtt_samples_;

    // Per packet number space (Initial, Handshake, Application)
    struct inflight_packet
    {
        std::uint64_t pn;
        std::uint64_t bytes;
        time_point sent_time;
        bool ack_eliciting;
    };

    struct pn_space_state
    {
        std::map<std::uint64_t, inflight_packet> in_flight_packets_{};
        std::uint64_t largest_sent_packet_number_ = 0;
        bool has_sent_packet_ = false;
        std::uint64_t largest_acked_pn_ = std::numeric_limits<std::uint64_t>::max();
        time_point largest_acked_pn_sent_time_ = time_point::min();
        time_point time_of_last_ack_eliciting_packet_ = time_point::min();
        std::uint32_t pto_count_ = 0;
        std::uint64_t bytes_in_flight_ = 0;
    };

    pn_space_state initial_space_{};
    pn_space_state handshake_space_{};
    pn_space_state app_space_{};

    auto get_pn_space_state(pn_space space) -> pn_space_state&
    {
        switch (space)
        {
        case pn_space::initial:
            return initial_space_;
        case pn_space::handshake:
            return handshake_space_;
        case pn_space::application:
            return app_space_;
        }
        return app_space_;
    }

    auto get_pn_space_state(pn_space space) const -> const pn_space_state&
    {
        switch (space)
        {
        case pn_space::initial:
            return initial_space_;
        case pn_space::handshake:
            return handshake_space_;
        case pn_space::application:
            return app_space_;
        }
        return app_space_;
    }

    auto remove_if_inflight(std::uint64_t pn, pn_space space) -> bool
    {
        auto& state = get_pn_space_state(space);
        const auto it = state.in_flight_packets_.find(pn);
        if (it != state.in_flight_packets_.end())
        {
            state.bytes_in_flight_ -= it->second.bytes;
            state.in_flight_packets_.erase(it);
            return true;
        }

        return false;
    }

    auto get_loss_time_for_space(pn_space, const pn_space_state& state)
        -> std::optional<time_point>
    {
        if (state.in_flight_packets_.empty())
        {
            return std::nullopt;
        }

        auto max_rtt = rtt_samples_.smoothed_rtt_;
        if (max_rtt == duration_type{})
            max_rtt = std::chrono::milliseconds(333);
        if (rtt_samples_.latest_rtt_ > max_rtt)
        {
            max_rtt = rtt_samples_.latest_rtt_;
        }

        const auto loss_delay = std::max(max_rtt + max_rtt / 8,
            duration_type{std::chrono::milliseconds(1)});
        auto loss_time = state.largest_acked_pn_sent_time_ + loss_delay;

        return loss_time;
    }

    auto pto_expires(time_point now, pn_space space) -> bool
    {
        auto& state = get_pn_space_state(space);

        if (state.in_flight_packets_.empty())
        {
            return false;
        }

        auto loss_time = get_loss_time_for_space(space, state);
        if (!loss_time.has_value())
        {
            return false;
        }

        return now >= *loss_time;
    }

    auto pto_duration_for(pn_space space) const noexcept -> duration_type
    {
        const auto smoothed_rtt = rtt_samples_.smoothed_rtt_ == duration_type{}
            ? duration_type{std::chrono::milliseconds(333)}
            : rtt_samples_.smoothed_rtt_;
        const auto rtt_var = rtt_samples_.rtt_var_ == duration_type{}
            ? smoothed_rtt / 2
            : rtt_samples_.rtt_var_;
        auto duration = smoothed_rtt +
            std::max<duration_type>(4 * rtt_var, std::chrono::milliseconds(1));
        if (space == pn_space::application)
            duration += std::chrono::milliseconds(25);
        return duration;
    }

    auto update_rtt_estimate(duration_type rtt, std::uint32_t ack_delay) -> void
    {
        // Update min_rtt
        if (rtt < rtt_samples_.min_rtt_)
        {
            rtt_samples_.min_rtt_ = rtt;
        }

        // Before peer transport parameters are plumbed into this component,
        // decode ACK delay using QUIC's default exponent (3) and cap it as
        // RFC 9002 §5.3 requires: it may never reduce RTT below min_rtt.
        const auto bounded_ack_delay = std::min<std::uint64_t>(ack_delay,
            static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)() >> 3));
        const auto encoded_delay = std::chrono::microseconds{
            static_cast<std::int64_t>(bounded_ack_delay << 3)};
        const auto delay = duration_type{encoded_delay};
        auto adjusted_rtt = rtt;
        if (rtt >= rtt_samples_.min_rtt_ + delay)
            adjusted_rtt -= delay;
        adjust_rtt_estimate(adjusted_rtt);

        rtt_samples_.latest_rtt_ = rtt;
    }

    void adjust_rtt_estimate(duration_type filtered_rtt)
    {
        if (rtt_samples_.smoothed_rtt_ == duration_type{})
        {
            rtt_samples_.smoothed_rtt_ = filtered_rtt;
            rtt_samples_.rtt_var_ = filtered_rtt / 2;
        }
        else
        {
            // SmoothedRTT = (1 - G) * SmootheRTT + G * filtered_RTT
            auto weighted_variance = rtt_samples_.rtt_var_;
            if (filtered_rtt > rtt_samples_.smoothed_rtt_)
            {
                weighted_variance = ((7 * weighted_variance) +
                                        (filtered_rtt - rtt_samples_.smoothed_rtt_)) /
                    8;
            }
            else
            {
                weighted_variance = ((7 * weighted_variance) +
                                        (rtt_samples_.smoothed_rtt_ - filtered_rtt)) /
                    8;
            }

            rtt_samples_.rtt_var_ = weighted_variance;

            const auto diff = filtered_rtt - rtt_samples_.smoothed_rtt_;
            rtt_samples_.smoothed_rtt_ += diff / 8;
        }
    }

    void update_rtt_estimate_internal(duration_type rtt)
    {
        if (rtt < rtt_samples_.min_rtt_)
        {
            rtt_samples_.min_rtt_ = rtt;
        }

        if (rtt_samples_.smoothed_rtt_ == duration_type{})
        {
            rtt_samples_.smoothed_rtt_ = rtt;
            rtt_samples_.rtt_var_ = rtt / 2;
        }
        else
        {
            auto weighted_variance = rtt_samples_.rtt_var_;
            if (rtt > rtt_samples_.smoothed_rtt_)
            {
                weighted_variance = ((7 * weighted_variance) + (rtt - rtt_samples_.smoothed_rtt_)) / 8;
            }
            else
            {
                weighted_variance = ((7 * weighted_variance) + (rtt_samples_.smoothed_rtt_ - rtt)) / 8;
            }

            rtt_samples_.rtt_var_ = weighted_variance;

            const auto diff = rtt - rtt_samples_.smoothed_rtt_;
            rtt_samples_.smoothed_rtt_ += diff / 8;
        }
    }
};

} // namespace cnetmod::quic
