module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.quic;

import std;
import :loss_detection;

namespace cnetmod::quic {

loss_detector::packet_node_cache::packet_node_cache(const packet_node_cache&) noexcept
{
}

auto loss_detector::packet_node_cache::operator=(const packet_node_cache& other) noexcept
    -> packet_node_cache&
{
    if (this != std::addressof(other))
        clear();
    return *this;
}

loss_detector::packet_node_cache::packet_node_cache(packet_node_cache&& other) noexcept
    : size(std::exchange(other.size, 0U))
{
    for (std::size_t index{}; index < size; ++index)
        nodes[index] = std::move(other.nodes[index]);
}

auto loss_detector::packet_node_cache::operator=(packet_node_cache&& other) noexcept
    -> packet_node_cache&
{
    if (this != std::addressof(other))
    {
        clear();
        size = std::exchange(other.size, 0U);
        for (std::size_t index{}; index < size; ++index)
            nodes[index] = std::move(other.nodes[index]);
    }
    return *this;
}

auto loss_detector::packet_node_cache::clear() noexcept -> void
{
    while (size != 0U)
        nodes[--size] = {};
}

auto loss_detector::discard_packet_number_space(pn_space space) noexcept -> std::uint64_t
{
    auto& state = get_pn_space_state(space);
    // Key discard is repeated for every 1-RTT packet. An already discarded
    // space must not construct or move an array of empty cache handles.
    if (!state.has_sent_packet_ && state.in_flight_packets_.empty() &&
        state.cached_nodes_.size == 0U)
        return 0U;
    const auto bytes = state.bytes_in_flight_;
    state.in_flight_packets_.clear();
    state.cached_nodes_.clear();
    state.largest_sent_packet_number_ = 0U;
    state.has_sent_packet_ = false;
    state.largest_acked_pn_ = std::numeric_limits<std::uint64_t>::max();
    state.largest_acked_pn_sent_time_ = time_point::min();
    state.time_of_last_ack_eliciting_packet_ = time_point::min();
    state.pto_count_ = 0U;
    state.bytes_in_flight_ = 0U;
    return bytes;
}

auto loss_detector::insert_inflight_packet(pn_space_state& state, inflight_packet packet) -> void
{
    auto& cache = state.cached_nodes_;
    if (cache.size == 0U)
    {
        state.in_flight_packets_.emplace(packet.pn, packet);
        return;
    }
    auto node = std::move(cache.nodes[--cache.size]);
    node.key() = packet.pn;
    node.mapped() = packet;
    state.in_flight_packets_.insert(std::move(node));
}

auto loss_detector::recycle_inflight_packet(pn_space_state& state,
    inflight_packet_map::iterator packet) noexcept -> inflight_packet_map::iterator
{
    const auto next = std::next(packet);
    auto node = state.in_flight_packets_.extract(packet);
    node.mapped() = {};
    auto& cache = state.cached_nodes_;
    if (cache.size < cache.nodes.size())
        cache.nodes[cache.size++] = std::move(node);
    return next;
}

} // namespace cnetmod::quic
