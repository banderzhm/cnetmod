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

auto quic_recovery_controller::sent_packet_store::slot_index(
    std::uint64_t packet_number) const noexcept -> std::size_t
{
    return static_cast<std::size_t>(packet_number) & (slots_.size() - 1U);
}

quic_recovery_controller::sent_packet_store::sent_packet_store(
    sent_packet_store&& other) noexcept
    : slots_(std::move(other.slots_)),
      overflow_(std::move(other.overflow_)),
      primary_size_(std::exchange(other.primary_size_, 0U))
{
    other.release();
}

auto quic_recovery_controller::sent_packet_store::operator=(
    sent_packet_store&& other) noexcept -> sent_packet_store&
{
    if (this != std::addressof(other))
    {
        slots_ = std::move(other.slots_);
        overflow_ = std::move(other.overflow_);
        primary_size_ = std::exchange(other.primary_size_, 0U);
        other.release();
    }
    return *this;
}

auto quic_recovery_controller::sent_packet_store::grow() -> void
{
    const auto capacity = slots_.empty() ? initial_capacity : slots_.size() * 2U;
    std::vector<packet_slot> grown(capacity);
    for (auto& slot : slots_)
    {
        if (!slot.packet)
            continue;
        auto& destination = grown[static_cast<std::size_t>(slot.packet_number) &
            (capacity - 1U)];
        destination.packet_number = slot.packet_number;
        destination.packet = std::move(slot.packet);
    }
    slots_ = std::move(grown);
}

auto quic_recovery_controller::sent_packet_store::ensure_available(
    std::uint64_t packet_number) -> bool
{
    if (slots_.empty())
        grow();
    for (;;)
    {
        const auto& slot = slots_[slot_index(packet_number)];
        if (!slot.packet || slot.packet_number == packet_number)
            return true;
        if (primary_size_ * 2U < slots_.size())
            return false;
        grow();
    }
}

auto quic_recovery_controller::sent_packet_store::insert(
    std::uint64_t packet_number, sent_packet packet) -> void
{
    if (auto* existing = find(packet_number))
    {
        *existing = std::move(packet);
        return;
    }
    if (!ensure_available(packet_number))
    {
        overflow_.emplace(packet_number, std::move(packet));
        return;
    }
    auto& slot = slots_[slot_index(packet_number)];
    slot.packet_number = packet_number;
    ++primary_size_;
    slot.packet = std::move(packet);
}

auto quic_recovery_controller::sent_packet_store::find(
    std::uint64_t packet_number) noexcept -> sent_packet*
{
    if (slots_.empty())
        return nullptr;
    auto& slot = slots_[slot_index(packet_number)];
    if (slot.packet && slot.packet_number == packet_number)
        return std::addressof(*slot.packet);
    const auto found = overflow_.find(packet_number);
    return found == overflow_.end() ? nullptr : std::addressof(found->second);
}

auto quic_recovery_controller::sent_packet_store::find(
    std::uint64_t packet_number) const noexcept -> const sent_packet*
{
    if (slots_.empty())
        return nullptr;
    const auto& slot = slots_[slot_index(packet_number)];
    if (slot.packet && slot.packet_number == packet_number)
        return std::addressof(*slot.packet);
    const auto found = overflow_.find(packet_number);
    return found == overflow_.end() ? nullptr : std::addressof(found->second);
}

auto quic_recovery_controller::sent_packet_store::erase(
    std::uint64_t packet_number) noexcept -> void
{
    if (slots_.empty())
        return;
    auto& slot = slots_[slot_index(packet_number)];
    if (slot.packet && slot.packet_number == packet_number)
    {
        slot.packet.reset();
        --primary_size_;
        return;
    }
    overflow_.erase(packet_number);
}

auto quic_recovery_controller::sent_packet_store::clear() noexcept -> void
{
    for (auto& slot : slots_)
        slot.packet.reset();
    overflow_.clear();
    primary_size_ = 0U;
}

auto quic_recovery_controller::sent_packet_store::release() noexcept -> void
{
    std::vector<packet_slot>{}.swap(slots_);
    overflow_.clear();
    primary_size_ = 0U;
}

auto quic_recovery_controller::sent_packet_store::ordered_packet_numbers() const
    -> std::vector<std::uint64_t>
{
    std::vector<std::uint64_t> result;
    result.reserve(primary_size_ + overflow_.size());
    for_each([&result](std::uint64_t packet_number, const sent_packet&)
        {
            result.push_back(packet_number);
        });
    std::ranges::sort(result);
    return result;
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
    sent_packets_[space_index(space)].insert(packet_number,
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
        auto* packet = sent.find(packet_number);
        if (packet == nullptr)
            continue;
        if (packet->path_mtu_probe_target)
            update.acknowledged_path_mtu_probes.push_back(
                *packet->path_mtu_probe_target);
        if (resolve_aliases)
        {
            packet->frames.for_each(
                [&delivered_frames](auto& delivered)
                {
                    delivered_frames.push_back(std::move(delivered));
                });
        }
        acknowledged_alias = acknowledged_alias || packet->retransmission_alias;
        if (packet->retransmission_alias)
            --alias_count;
        congestion_.on_packet_acked(packet->bytes);
        sent.erase(packet_number);
    }

    if (!delivered_frames.empty())
    {
        sent.for_each([&](std::uint64_t, sent_packet& packet)
            {
                if (!acknowledged_alias && !packet.retransmission_alias)
                    return;
                packet.frames.erase_if(
                    [&delivered_frames](const auto& candidate)
                    {
                        return std::ranges::find(delivered_frames, candidate) !=
                            delivered_frames.end();
                    });
            });
    }
    congestion_.update_rtt(detector_.rtt_estimate().smoothed_rtt_);

    const auto lost = detector_.detect_lost_packets(received_at, space);
    std::uint64_t lost_bytes{};
    std::uint64_t lost_probe_bytes{};
    for (const auto packet_number : lost)
    {
        auto* packet = sent.find(packet_number);
        if (packet == nullptr)
            continue;
        if (packet->path_mtu_probe_target)
        {
            update.lost_path_mtu_probes.push_back(
                *packet->path_mtu_probe_target);
            lost_probe_bytes += packet->bytes;
        }
        else
            lost_bytes += packet->bytes;
        packet->frames.for_each_reverse(
            [&update](auto& frame)
            {
                update.lost_frames.push_back(std::move(frame));
            });
        if (packet->retransmission_alias)
            --alias_count;
        sent.erase(packet_number);
    }
    // RFC 9000 section 14.4: a lost PMTU probe consumes flight credit but
    // is not a reliable congestion signal. Ordinary losses still reduce cwnd.
    if (lost_probe_bytes != 0U)
        congestion_.on_packets_discarded(lost_probe_bytes);
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
    const sent_packet* newest{};
    std::uint64_t newest_packet_number{};
    sent.for_each([&](std::uint64_t packet_number, const sent_packet& packet)
        {
            if (!packet.frames.empty() &&
                (newest == nullptr || packet_number > newest_packet_number))
            {
                newest = std::addressof(packet);
                newest_packet_number = packet_number;
            }
        });
    return newest == nullptr
        ? std::nullopt
        : std::optional{newest->frames};
}

auto quic_recovery_controller::discard_non_retransmittable(pn_space space)
    -> std::vector<std::size_t>
{
    auto& sent = sent_packets_[space_index(space)];
    std::vector<std::size_t> probes;
    std::uint64_t lost_bytes{};
    std::uint64_t lost_probe_bytes{};
    sent.for_each([&](std::uint64_t packet_number, const sent_packet& packet)
        {
            if (packet.path_mtu_probe_target)
            {
                probes.push_back(*packet.path_mtu_probe_target);
                lost_probe_bytes += packet.bytes;
            }
            else
                lost_bytes += packet.bytes;
            detector_.discard_inflight_packet(packet_number, space);
        });
    sent.clear();
    alias_counts_[space_index(space)] = 0U;
    // RFC 9000 section 14.4: a lost PMTU probe consumes flight credit but
    // is not a reliable congestion signal. Ordinary losses still reduce cwnd.
    if (lost_probe_bytes != 0U)
        congestion_.on_packets_discarded(lost_probe_bytes);
    if (lost_bytes != 0U)
        congestion_.on_congestion_event(lost_bytes);
    return probes;
}

auto quic_recovery_controller::take_outstanding_frames(pn_space space)
    -> std::vector<quic_send_frame>
{
    auto& sent = sent_packets_[space_index(space)];
    std::vector<quic_send_frame> result;
    for (const auto packet_number : sent.ordered_packet_numbers())
        sent.find(packet_number)->frames.for_each([&result](auto& frame)
            {
                result.push_back(std::move(frame));
            });
    return result;
}

void quic_recovery_controller::discard_packet_number_space(pn_space space) noexcept
{
    sent_packets_[space_index(space)].release();
    alias_counts_[space_index(space)] = 0U;
    congestion_.on_packets_discarded(detector_.discard_packet_number_space(space));
}

auto quic_recovery_controller::take_early_data_frames() -> std::vector<quic_send_frame>
{
    auto& sent = sent_packets_[space_index(pn_space::application)];
    std::vector<quic_send_frame> result;
    std::uint64_t discarded{};
    for (const auto packet_number : sent.ordered_packet_numbers())
    {
        auto* packet = sent.find(packet_number);
        if (packet == nullptr)
            continue;
        const bool contained_early = packet->frames.any_of(
            [](const auto& frame)
            {
                return frame.is_early_data();
            });
        packet->frames.erase_if([&result](auto& frame)
            {
                if (!frame.is_early_data())
                    return false;
                if (std::ranges::find(result, frame) == result.end())
                    result.push_back(std::move(frame));
                return true;
            });
        if (contained_early && packet->frames.empty() && !packet->path_mtu_probe_target)
        {
            detector_.discard_inflight_packet(packet_number, pn_space::application);
            discarded += packet->bytes;
            if (packet->retransmission_alias)
                --alias_counts_[space_index(pn_space::application)];
            sent.erase(packet_number);
        }
    }
    congestion_.on_packets_discarded(discarded);
    return result;
}

auto quic_recovery_controller::on_max_datagram_size_validated(std::uint64_t bytes) noexcept -> void
{
    congestion_.on_max_datagram_size_validated(bytes);
}

auto quic_recovery_controller::can_send_datagram(std::uint64_t bytes) const noexcept -> bool
{
    return congestion_.can_send_datagram(bytes);
}

auto quic_recovery_controller::remaining_send_credit() const noexcept -> std::uint64_t
{
    const auto window = congestion_.congestion_window();
    return window - std::min(window, congestion_.bytes_in_flight());
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
