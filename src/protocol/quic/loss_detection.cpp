module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.quic;

import std;
import :loss_detection;

namespace cnetmod::quic {

auto loss_detector::valid_ack_ranges(const ack_frame& frame,
    std::uint64_t largest_acked_pn) noexcept -> bool
{
    if (frame.first_ack_range > largest_acked_pn)
        return false;
    auto range_low = largest_acked_pn - frame.first_ack_range;
    for (const auto& range : frame.ack_ranges)
    {
        // Subtract only after checking both operands. In particular, adding
        // two to a public ack_range::gap can wrap at UINT64_MAX.
        if (range_low < 2U || range.gap > range_low - 2U)
            return false;
        const auto range_high = range_low - 2U - range.gap;
        if (range.ack_range_length > range_high)
            return false;
        range_low = range_high - range.ack_range_length;
    }
    return true;
}

auto loss_detector::inflight_packet_store::slot_index(
    std::uint64_t packet_number) const noexcept -> std::size_t
{
    return static_cast<std::size_t>(packet_number) & (slots_.size() - 1U);
}

loss_detector::inflight_packet_store::inflight_packet_store(
    inflight_packet_store&& other) noexcept
    : slots_(std::move(other.slots_)),
      overflow_(std::move(other.overflow_)),
      primary_size_(std::exchange(other.primary_size_, 0U))
{
    other.release();
}

auto loss_detector::inflight_packet_store::operator=(
    inflight_packet_store&& other) noexcept -> inflight_packet_store&
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

auto loss_detector::inflight_packet_store::grow() -> void
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

auto loss_detector::inflight_packet_store::ensure_available(
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

auto loss_detector::inflight_packet_store::insert(inflight_packet packet) -> void
{
    const auto packet_number = packet.pn;
    if (auto* existing = find(packet_number))
    {
        *existing = packet;
        return;
    }
    if (!ensure_available(packet_number))
    {
        overflow_.emplace(packet_number, packet);
        return;
    }
    auto& slot = slots_[slot_index(packet_number)];
    slot.packet_number = packet_number;
    ++primary_size_;
    slot.packet = packet;
}

auto loss_detector::inflight_packet_store::find(
    std::uint64_t packet_number) noexcept -> inflight_packet*
{
    if (slots_.empty())
        return nullptr;
    auto& slot = slots_[slot_index(packet_number)];
    if (slot.packet && slot.packet_number == packet_number)
        return std::addressof(*slot.packet);
    const auto found = overflow_.find(packet_number);
    return found == overflow_.end() ? nullptr : std::addressof(found->second);
}

auto loss_detector::inflight_packet_store::find(
    std::uint64_t packet_number) const noexcept -> const inflight_packet*
{
    if (slots_.empty())
        return nullptr;
    const auto& slot = slots_[slot_index(packet_number)];
    if (slot.packet && slot.packet_number == packet_number)
        return std::addressof(*slot.packet);
    const auto found = overflow_.find(packet_number);
    return found == overflow_.end() ? nullptr : std::addressof(found->second);
}

auto loss_detector::inflight_packet_store::erase(
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

auto loss_detector::inflight_packet_store::release() noexcept -> void
{
    std::vector<packet_slot>{}.swap(slots_);
    overflow_.clear();
    primary_size_ = 0U;
}

auto loss_detector::discard_packet_number_space(pn_space space) noexcept -> std::uint64_t
{
    auto& state = get_pn_space_state(space);
    // Key discard is repeated for every 1-RTT packet. An already discarded
    // space must not construct or move an array of empty cache handles.
    if (!state.has_sent_packet_ && state.in_flight_packets_.empty())
        return 0U;
    const auto bytes = state.bytes_in_flight_;
    state.in_flight_packets_.release();
    state.largest_sent_packet_number_ = 0U;
    state.has_sent_packet_ = false;
    state.largest_acked_pn_ = std::numeric_limits<std::uint64_t>::max();
    state.largest_acked_pn_sent_time_ = time_point::min();
    state.time_of_last_ack_eliciting_packet_ = time_point::min();
    state.pto_count_ = 0U;
    state.bytes_in_flight_ = 0U;
    return bytes;
}

} // namespace cnetmod::quic
