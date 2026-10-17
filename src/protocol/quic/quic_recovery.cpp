module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.quic;

import std;
import :recovery;

namespace cnetmod::quic {

retransmittable_frame_storage::retransmittable_frame_storage(quic_send_frame frame)
    : first_(std::move(frame))
{
}

auto retransmittable_frame_storage::empty() const noexcept -> bool
{
    return first_.empty();
}

auto retransmittable_frame_storage::size() const noexcept -> std::size_t
{
    return first_.empty() ? 0U : 1U + additional_.size();
}

void retransmittable_frame_storage::push_back(quic_send_frame frame)
{
    if (first_.empty())
        first_ = std::move(frame);
    else
        additional_.push_back(std::move(frame));
}

auto retransmittable_frame_storage::operator[](std::size_t index) -> quic_send_frame&
{
    return index == 0U ? first_ : additional_[index - 1U];
}

auto retransmittable_frame_storage::operator[](std::size_t index) const
    -> const quic_send_frame&
{
    return index == 0U ? first_ : additional_[index - 1U];
}

quic_recovery_controller::quic_recovery_controller(const quic_config& config)
    : detector_(config), congestion_(config)
{
}

auto quic_recovery_controller::space_index(pn_space space) noexcept -> std::size_t
{
    return static_cast<std::size_t>(space);
}

void quic_recovery_controller::on_packet_sent(std::uint64_t packet_number,
    std::size_t bytes, time_point sent_at, bool recovery_tracked, pn_space space,
    retransmittable_frame_storage frames,
    std::optional<std::size_t> path_mtu_probe_target,
    bool retransmission_alias)
{
    detector_.on_packet_sent(packet_number, bytes, sent_at, recovery_tracked, space);
    if (!recovery_tracked)
        return;
    congestion_.on_packet_sent(bytes);
    sent_packets_[space_index(space)].emplace(packet_number,
        sent_packet{std::move(frames), bytes, path_mtu_probe_target,
            retransmission_alias});
    if (retransmission_alias)
        ++alias_counts_[space_index(space)];
}

auto quic_recovery_controller::on_ack_received(const ack_frame& frame,
    time_point received_at, pn_space space)
    -> std::expected<quic_recovery_update, std::error_code>
{
    auto acknowledged = detector_.on_ack_received(
        frame, frame.largest_acked, received_at, space);
    if (!acknowledged)
        return std::unexpected(acknowledged.error());

    auto& sent = sent_packets_[space_index(space)];
    auto& alias_count = alias_counts_[space_index(space)];
    const bool resolve_aliases = alias_count != 0U;
    std::vector<quic_send_frame> delivered_frames;
    if (resolve_aliases)
        delivered_frames.reserve(acknowledged->size());
    bool acknowledged_alias{};
    quic_recovery_update update;

    for (const auto packet_number : *acknowledged)
    {
        const auto packet = sent.find(packet_number);
        if (packet == sent.end())
            continue;
        if (packet->second.path_mtu_probe_target)
            update.acknowledged_path_mtu_probes.push_back(
                *packet->second.path_mtu_probe_target);
        if (resolve_aliases)
        {
            packet->second.frames.for_each(
                [&delivered_frames](auto& delivered)
                {
                    delivered_frames.push_back(std::move(delivered));
                });
        }
        acknowledged_alias = acknowledged_alias || packet->second.retransmission_alias;
        if (packet->second.retransmission_alias)
            --alias_count;
        congestion_.on_packet_acked(packet->second.bytes);
        sent.erase(packet);
    }

    if (!delivered_frames.empty())
    {
        for (auto& [_, packet] : sent)
        {
            if (!acknowledged_alias && !packet.retransmission_alias)
                continue;
            packet.frames.erase_if(
                [&delivered_frames](const auto& candidate)
                {
                    return std::ranges::find(delivered_frames, candidate) !=
                        delivered_frames.end();
                });
        }
    }
    congestion_.update_rtt(detector_.rtt_estimate().smoothed_rtt_);

    const auto lost = detector_.detect_lost_packets(received_at, space);
    std::uint64_t lost_bytes{};
    for (const auto packet_number : lost)
    {
        const auto packet = sent.find(packet_number);
        if (packet == sent.end())
            continue;
        if (packet->second.path_mtu_probe_target)
            update.lost_path_mtu_probes.push_back(
                *packet->second.path_mtu_probe_target);
        lost_bytes += packet->second.bytes;
        packet->second.frames.for_each_reverse(
            [&update](auto& frame)
            {
                update.lost_frames.push_back(std::move(frame));
            });
        if (packet->second.retransmission_alias)
            --alias_count;
        sent.erase(packet);
    }
    if (lost_bytes != 0U)
        congestion_.on_congestion_event(lost_bytes);
    return update;
}

auto quic_recovery_controller::next_pto_deadline() const
    -> std::optional<std::pair<time_point, pn_space>>
{
    return detector_.next_pto_deadline();
}

auto quic_recovery_controller::pto_duration() const noexcept
    -> std::chrono::steady_clock::duration
{
    return detector_.pto_duration();
}

void quic_recovery_controller::on_pto_expired(pn_space space) noexcept
{
    detector_.on_pto_expired(space);
}

auto quic_recovery_controller::newest_probe_frames(pn_space space) const
    -> std::optional<retransmittable_frame_storage>
{
    const auto& sent = sent_packets_[space_index(space)];
    const auto packet = std::ranges::find_if(sent.rbegin(), sent.rend(),
        [](const auto& entry)
        {
            return !entry.second.frames.empty();
        });
    return packet == sent.rend()
        ? std::nullopt
        : std::optional{packet->second.frames};
}

auto quic_recovery_controller::discard_non_retransmittable(pn_space space)
    -> std::vector<std::size_t>
{
    auto& sent = sent_packets_[space_index(space)];
    std::vector<std::size_t> probes;
    std::uint64_t lost_bytes{};
    for (const auto& [packet_number, packet] : sent)
    {
        if (packet.path_mtu_probe_target)
            probes.push_back(*packet.path_mtu_probe_target);
        detector_.discard_inflight_packet(packet_number, space);
        lost_bytes += packet.bytes;
    }
    sent.clear();
    alias_counts_[space_index(space)] = 0U;
    if (lost_bytes != 0U)
        congestion_.on_congestion_event(lost_bytes);
    return probes;
}

auto quic_recovery_controller::take_outstanding_frames(pn_space space)
    -> std::vector<quic_send_frame>
{
    auto& sent = sent_packets_[space_index(space)];
    std::vector<quic_send_frame> result;
    for (auto& [_, packet] : sent)
        packet.frames.for_each(
            [&result](auto& frame)
            {
                result.push_back(std::move(frame));
            });
    return result;
}

void quic_recovery_controller::discard_packet_number_space(pn_space space) noexcept
{
    sent_packets_[space_index(space)].clear();
    alias_counts_[space_index(space)] = 0U;
    congestion_.on_packets_discarded(detector_.discard_packet_number_space(space));
}

auto quic_recovery_controller::can_send_datagram(std::uint64_t bytes) const noexcept -> bool
{
    return congestion_.can_send_datagram(bytes);
}

auto quic_recovery_controller::congestion_window() const noexcept -> std::uint64_t
{
    return congestion_.congestion_window();
}

auto quic_recovery_controller::pacing_rate() const noexcept -> std::optional<double>
{
    return congestion_.pacing_rate();
}

} // namespace cnetmod::quic
