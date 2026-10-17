module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.quic:recovery;

import std;
import :foundation;
import :frame;
import :loss_detection;
import :congestion_control;
import cnetmod.protocol.quic.send_pipeline;

namespace cnetmod::quic {

/// Small-buffer storage for the retransmittable frames owned by one packet.
/// Most QUIC packets carry one frame, so that case does not allocate a second
/// container. Shared response frames retain only their body slice descriptor.
export class retransmittable_frame_storage
{
public:
    retransmittable_frame_storage() = default;
    explicit retransmittable_frame_storage(quic_send_frame frame);

    [[nodiscard]] auto empty() const noexcept -> bool;
    [[nodiscard]] auto size() const noexcept -> std::size_t;
    auto push_back(quic_send_frame frame) -> void;
    [[nodiscard]] auto operator[](std::size_t index) -> quic_send_frame&;
    [[nodiscard]] auto operator[](std::size_t index) const
        -> const quic_send_frame&;

    template <class Function>
    auto for_each(Function&& function) -> void
    {
        if (!first_.empty())
            function(first_);
        for (auto& frame : additional_)
            function(frame);
    }

    template <class Function>
    auto for_each(Function&& function) const -> void
    {
        if (!first_.empty())
            function(first_);
        for (const auto& frame : additional_)
            function(frame);
    }

    template <class Function>
    auto for_each_reverse(Function&& function) -> void
    {
        for (auto frame = additional_.rbegin(); frame != additional_.rend(); ++frame)
            function(*frame);
        if (!first_.empty())
            function(first_);
    }

    template <class Function>
    auto for_each_reverse(Function&& function) const -> void
    {
        for (auto frame = additional_.rbegin(); frame != additional_.rend(); ++frame)
            function(*frame);
        if (!first_.empty())
            function(first_);
    }

    template <class Predicate>
    auto erase_if(Predicate&& predicate) -> void
    {
        std::erase_if(additional_, predicate);
        if (!first_.empty() && predicate(first_))
        {
            if (additional_.empty())
                first_.clear();
            else
            {
                first_ = std::move(additional_.front());
                additional_.erase(additional_.begin());
            }
        }
    }

    template <class Predicate>
    [[nodiscard]] auto any_of(Predicate&& predicate) const -> bool
    {
        return (!first_.empty() && predicate(first_)) ||
            std::ranges::any_of(additional_, predicate);
    }

private:
    quic_send_frame first_;
    std::vector<quic_send_frame> additional_;
};

/// Events produced by applying an ACK and the resulting loss-detection pass.
/// The connection owns policy (frame requeue and PMTU scheduling); this value
/// carries only recovery facts across that boundary.
export struct quic_recovery_update
{
    std::vector<quic_send_frame> lost_frames;
    std::vector<std::size_t> acknowledged_path_mtu_probes;
    std::vector<std::size_t> lost_path_mtu_probes;
};

/// Per packet-number-space recovery coordinator. It is the single owner of
/// the RFC 9002 detector, congestion controller, sent-packet ledger and PTO
/// alias resolution. quic_connection reacts to the emitted events but no
/// longer edits recovery bookkeeping directly.
export class quic_recovery_controller
{
public:
    explicit quic_recovery_controller(const quic_config& config);

    auto on_packet_sent(std::uint64_t packet_number, std::size_t bytes,
        time_point sent_at, bool recovery_tracked, pn_space space,
        retransmittable_frame_storage frames = {},
        std::optional<std::size_t> path_mtu_probe_target = std::nullopt,
        bool retransmission_alias = false) -> void;

    [[nodiscard]] auto on_ack_received(const ack_frame& frame,
        time_point received_at, pn_space space)
        -> std::expected<quic_recovery_update, std::error_code>;

    [[nodiscard]] auto next_pto_deadline() const
        -> std::optional<std::pair<time_point, pn_space>>;
    [[nodiscard]] auto pto_duration() const noexcept
        -> std::chrono::steady_clock::duration;
    auto on_pto_expired(pn_space space) noexcept -> void;

    [[nodiscard]] auto newest_probe_frames(pn_space space) const
        -> std::optional<retransmittable_frame_storage>;
    [[nodiscard]] auto discard_non_retransmittable(pn_space space)
        -> std::vector<std::size_t>;
    [[nodiscard]] auto take_outstanding_frames(pn_space space)
        -> std::vector<quic_send_frame>;
    auto discard_packet_number_space(pn_space space) noexcept -> void;

    [[nodiscard]] auto can_send_datagram(std::uint64_t bytes) const noexcept -> bool;
    [[nodiscard]] auto congestion_window() const noexcept -> std::uint64_t;
    [[nodiscard]] auto pacing_rate() const noexcept -> std::optional<double>;

private:
    struct sent_packet
    {
        retransmittable_frame_storage frames;
        std::size_t bytes{};
        std::optional<std::size_t> path_mtu_probe_target;
        bool retransmission_alias{};
    };

    static constexpr std::size_t packet_number_space_count{3U};
    [[nodiscard]] static auto space_index(pn_space space) noexcept -> std::size_t;

    loss_detector detector_;
    congestion_controller congestion_;
    std::array<std::map<std::uint64_t, sent_packet>, packet_number_space_count>
        sent_packets_;
    std::array<std::size_t, packet_number_space_count> alias_counts_{};
};

} // namespace cnetmod::quic
