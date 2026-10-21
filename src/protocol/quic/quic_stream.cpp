module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.quic;

import std;

#ifdef CNETMOD_HAS_SSL
    #ifdef CNETMOD_ENABLE_QUIC

import :stream;

namespace cnetmod::quic {

// =============================================================================
// quic_stream Implementation
// =============================================================================

quic_stream::quic_stream(stream_id id, quic_role owner, bool bidirectional)
{
    impl_->id_ = id;
    impl_->owner_ = owner;
    impl_->bidirectional_ = bidirectional;
}

quic_stream::~quic_stream() = default;

auto quic_stream::init() -> void
{
    if (impl_->state_ != stream_state::idle)
    {
        return;
    }

    if (!impl_->bidirectional_)
    {
        const bool local = is_client_initiated(impl_->id_) ==
            (impl_->owner_ == quic_role::client);
        impl_->state_ = local ? stream_state::half_closed_remote
                             : stream_state::half_closed_local;
        return;
    }

    // For client-initiated streams, immediately mark as open locally
    if ((impl_->owner_ == quic_role::client && is_client_initiated(impl_->id_)) ||
        (impl_->owner_ == quic_role::server && is_server_initiated(impl_->id_)))
    {
        impl_->state_ = stream_state::open;
    }
}

auto quic_stream::send(std::span<const std::byte> data)
    -> task<std::expected<void, std::error_code>>
{
    co_return apply_send_state(data);
}

auto quic_stream::apply_send_state(std::span<const std::byte> data)
    -> std::expected<void, std::error_code>
{
    auto& impl = *impl_;

    // Check state
    if (impl.state_ == stream_state::closed ||
        impl.state_ == stream_state::half_closed_local)
    {
        return std::unexpected(std::make_error_code(std::errc::operation_not_permitted));
    }

    // Check flow control limit
    if (data.size() > impl.max_send_data_ - impl.total_sent_)
    {
        return std::unexpected(make_error_code(quic_errc::flow_control_error));
    }
    else
    {
        impl.total_sent_ += data.size();

        // Deliver directly if possible
        // In real implementation, this would be buffered and sent in packets

        if (data.empty())
        {
            impl.state_ = impl.state_ == stream_state::half_closed_remote
                ? stream_state::closed
                : stream_state::half_closed_local;
        }
    }

    return {};
}

auto quic_stream::close_local() -> task<void>
{
    apply_close_local_state();
    co_return;
}

auto quic_stream::apply_close_local_state() -> void
{
    if (impl_->state_ == stream_state::closed)
        return;
    impl_->state_ = impl_->state_ == stream_state::half_closed_remote
        ? stream_state::closed
        : stream_state::half_closed_local;
    return;
}

auto quic_stream::close_remote() -> void
{
    if (impl_->state_ == stream_state::closed)
        return;
    impl_->state_ = impl_->state_ == stream_state::half_closed_local
        ? stream_state::closed
        : stream_state::half_closed_remote;
}

auto quic_stream::close_both() -> task<void>
{
    impl_->state_ = stream_state::closed;
    co_return;
}

auto quic_stream::receive(mutable_buffer buf)
    -> task<std::expected<std::size_t, std::error_code>>
{
    co_return apply_receive_state(buf);
}

auto quic_stream::apply_receive_state(mutable_buffer buf)
    -> std::expected<std::size_t, std::error_code>
{
    auto& impl = *impl_;

    if (impl.receive_abandoned_)
        return std::unexpected(std::make_error_code(std::errc::operation_canceled));

    if (impl.state_ == stream_state::closed)
    {
        return std::size_t{0};
    }

    if (impl.receive_buffer_.empty())
    {
        if (impl.state_ == stream_state::half_closed_remote)
        {
            return std::size_t{0};
        }

        // Need to wait for more data
        return std::unexpected(std::make_error_code(std::errc::operation_would_block));
    }

    const auto total_copied = deliver_contiguous_data(buf);

    if (total_copied > 0)
    {
        // Notify sender that window has opened
    }
    if (impl.final_size_ && *impl.final_size_ == impl.next_expected_offset_ &&
        impl.receive_buffer_.empty())
    {
        impl.state_ = impl.state_ == stream_state::half_closed_local
            ? stream_state::closed
            : stream_state::half_closed_remote;
    }

    // Buffered bytes beyond a gap are not readable yet and are not EOF.  A
    // zero result is reserved for a consumed FIN/reset; callers use
    // operation_would_block to await the missing contiguous range.
    if (total_copied == 0 && impl.state_ != stream_state::closed &&
        impl.state_ != stream_state::half_closed_remote)
        return std::unexpected(
            std::make_error_code(std::errc::operation_would_block));

    return total_copied;
}

void quic_stream::update_send_limit(std::uint64_t maximum) noexcept
{
    impl_->max_send_data_ = std::max(impl_->max_send_data_, maximum);
}

void quic_stream::set_initial_send_limit(std::uint64_t maximum) noexcept
{
    impl_->max_send_data_ = maximum;
}

void quic_stream::set_initial_receive_limit(std::uint64_t maximum) noexcept
{
    // Called during stream creation, before authenticated bytes are accepted.
    impl_->max_data_ = maximum;
}

void quic_stream::extend_receive_limit(std::uint64_t maximum) noexcept
{
    impl_->max_data_ = std::max(impl_->max_data_, maximum);
}

auto quic_stream::deliver_contiguous_data(mutable_buffer buf) -> std::size_t
{
    std::size_t copied{};
    while (copied < buf.size)
    {
        auto it = impl_->receive_buffer_.find(impl_->next_expected_offset_);
        if (it == impl_->receive_buffer_.end())
            break;

        const auto available = it->second.size();
        const auto count = std::min(buf.size - copied, available);
        std::memcpy(static_cast<std::byte*>(buf.data) + copied, it->second.data(), count);
        if (count == available)
        {
            impl_->receive_buffer_.erase(it);
        }
        else
        {
            // byte move/erase cannot throw or allocate. Retain this segment's
            // capacity until it is consumed, reset or abandoned instead of
            // allocating a new tail after some bytes have already been read.
            it->second.erase(it->second.begin(), it->second.begin() + count);
            auto node = impl_->receive_buffer_.extract(it);
            node.key() += count;
            const auto inserted = impl_->receive_buffer_.insert(std::move(node));
            // push_received stores non-overlapping ranges; this key remains
            // inside the old range, strictly before every following range.
            if (!inserted.inserted)
                std::terminate(); // Never silently discard a rejected node.
        }
        copied += count;
        impl_->next_expected_offset_ += count;
    }
    return copied;
}

auto quic_stream::drain_receive_buffer() -> task<void>
{
    // Deliver any contiguous data at current offset
    if (!impl_->receive_buffer_.empty() &&
        impl_->receive_buffer_.begin()->first == impl_->next_expected_offset_)
    {
        // Buffer full enough, notify waiting reads
        co_return;
    }

    co_return;
}

auto quic_stream::receive_until_delimiter(
    char delim,
    dynamic_buffer& out)
    -> task<std::expected<std::size_t, std::error_code>>
{
    constexpr std::size_t chunk_size = 1024;
    while (true)
    {
        auto writable = out.prepare(chunk_size);
        auto result = co_await receive(writable);
        if (!result)
            co_return std::unexpected(result.error());
        out.commit(*result);
        const auto readable = out.data();
        const auto* bytes = static_cast<const std::byte*>(readable.data);
        for (std::size_t i = 0; i < readable.size; ++i)
            if (static_cast<char>(bytes[i]) == delim)
                co_return i + 1;
        if (*result == 0)
            co_return readable.size;
    }
}

auto quic_stream::push_received(std::uint64_t offset,
    std::span<const std::byte> data, bool fin)
    -> std::expected<void, std::error_code>
{
    auto& state = *impl_;
    if (offset > std::numeric_limits<std::uint64_t>::max() - data.size())
        return std::unexpected(make_error_code(quic_errc::final_size_error));
    const auto end = offset + data.size();
    if (fin && end < state.highest_received_offset_)
        return std::unexpected(make_error_code(quic_errc::final_size_error));
    if (state.final_size_ &&
        (end > *state.final_size_ || (fin && end != *state.final_size_)))
        return std::unexpected(make_error_code(quic_errc::final_size_error));
    // Retransmission after a consumed FIN is legal.  The bytes were already
    // authenticated and accounted, so accept an exact in-range duplicate
    // without reopening the receive direction.
    if (state.state_ == stream_state::closed ||
        state.state_ == stream_state::half_closed_remote)
        return end <= state.next_expected_offset_
            ? std::expected<void, std::error_code>{}
            : std::unexpected(make_error_code(quic_errc::final_size_error));
    if (offset > state.max_data_ || data.size() > state.max_data_ - offset)
    {
        return std::unexpected(make_error_code(quic_errc::flow_control_error));
    }
    if (fin)
    {
        if (state.final_size_ && *state.final_size_ != end)
            return std::unexpected(make_error_code(quic_errc::final_size_error));
        state.final_size_ = end;
    }
    if (state.receive_abandoned_)
    {
        state.highest_received_offset_ = std::max(state.highest_received_offset_, end);
        if (fin)
            close_remote();
        return {};
    }

    // Already-consumed bytes and exact duplicates do not alter the receive
    // sequence.  Overlapping, non-identical ranges are rejected rather than
    // silently corrupting an authenticated byte stream.
    if (end <= state.next_expected_offset_)
    {
        if (state.final_size_ &&
            *state.final_size_ == state.next_expected_offset_ &&
            state.receive_buffer_.empty())
            state.state_ = state.state_ == stream_state::half_closed_local
                ? stream_state::closed
                : stream_state::half_closed_remote;
        return {};
    }
    if (offset < state.next_expected_offset_)
    {
        data = data.subspan(state.next_expected_offset_ - offset);
        offset = state.next_expected_offset_;
    }
    // RFC 9000 permits duplicate and partially overlapping STREAM ranges, but
    // overlapping bytes must be identical.  Insert only holes so a peer cannot
    // replace already authenticated stream data by choosing a new offset.
    std::uint64_t cursor = offset;
    auto it = state.receive_buffer_.lower_bound(offset);
    if (it != state.receive_buffer_.begin())
    {
        const auto previous = std::prev(it);
        if (previous->first + previous->second.size() > offset)
            it = previous;
    }
    for (; it != state.receive_buffer_.end() && it->first < end; ++it)
    {
        const auto existing_begin = it->first;
        const auto existing_end = existing_begin + it->second.size();
        const auto overlap_begin = std::max(offset, existing_begin);
        const auto overlap_end = std::min(end, existing_end);
        if (overlap_begin < overlap_end)
        {
            const auto incoming_offset = static_cast<std::size_t>(overlap_begin - offset);
            const auto existing_offset = static_cast<std::size_t>(overlap_begin - existing_begin);
            const auto overlap_size = static_cast<std::size_t>(overlap_end - overlap_begin);
            if (!std::equal(data.begin() + incoming_offset,
                    data.begin() + incoming_offset + overlap_size,
                    it->second.begin() + existing_offset))
                return std::unexpected(make_error_code(quic_errc::protocol_violation));
        }
        if (cursor < existing_begin)
        {
            const auto hole_end = std::min(end, existing_begin);
            state.receive_buffer_.try_emplace(cursor,
                data.begin() + static_cast<std::size_t>(cursor - offset),
                data.begin() + static_cast<std::size_t>(hole_end - offset));
        }
        cursor = std::max(cursor, existing_end);
    }
    if (cursor < end)
        state.receive_buffer_.try_emplace(cursor,
            data.begin() + static_cast<std::size_t>(cursor - offset), data.end());
    state.highest_received_offset_ = std::max(state.highest_received_offset_, end);
    if (state.state_ != stream_state::half_closed_local)
        state.state_ = stream_state::open;
    if (state.final_size_ && *state.final_size_ == state.next_expected_offset_ &&
        state.receive_buffer_.empty())
        state.state_ = state.state_ == stream_state::half_closed_local
            ? stream_state::closed
            : stream_state::half_closed_remote;
    return {};
}

auto quic_stream::reset_remote(std::uint64_t final_size)
    -> std::expected<void, std::error_code>
{
    auto& state = *impl_;
    if (state.final_size_ && *state.final_size_ != final_size)
        return std::unexpected(make_error_code(quic_errc::final_size_error));
    if (state.highest_received_offset_ > final_size)
        return std::unexpected(make_error_code(quic_errc::final_size_error));
    if (final_size > state.max_data_)
        return std::unexpected(make_error_code(quic_errc::flow_control_error));
    state.final_size_ = final_size;
    state.highest_received_offset_ = final_size;
    state.next_expected_offset_ = final_size;
    state.receive_buffer_.clear();
    state.state_ = state.state_ == stream_state::closed ||
            state.state_ == stream_state::half_closed_local
        ? stream_state::closed
        : stream_state::half_closed_remote;
    return {};
}

void quic_stream::stop_local() noexcept
{
    if (impl_->state_ == stream_state::half_closed_remote)
        impl_->state_ = stream_state::closed;
    else if (impl_->state_ != stream_state::closed)
        impl_->state_ = stream_state::half_closed_local;
}

void quic_stream::abandon_receive() noexcept
{
    impl_->receive_abandoned_ = true;
    impl_->receive_buffer_.clear();
    if (impl_->final_size_)
        close_remote();
}

auto quic_stream::receive_abandoned() const noexcept -> bool
{
    return impl_->receive_abandoned_;
}

auto quic_stream::state() const noexcept -> stream_state
{
    return impl_->state_;
}

auto quic_stream::is_readable() const noexcept -> bool
{
    if (impl_->receive_abandoned_)
        return true;
    if (impl_->state_ == stream_state::closed ||
        impl_->state_ == stream_state::half_closed_remote)
        return true;
    return impl_->receive_buffer_.contains(impl_->next_expected_offset_);
}

auto quic_stream::is_writable() const noexcept -> bool
{
    return impl_->state_ != stream_state::half_closed_local &&
        impl_->state_ != stream_state::closed;
}

auto quic_stream::remaining_send_window() const noexcept -> std::uint64_t
{
    return impl_->max_send_data_ > impl_->total_sent_
        ? impl_->max_send_data_ - impl_->total_sent_ : 0U;
}

auto quic_stream::remaining_receive_window() const noexcept -> std::uint64_t
{
    return impl_->max_data_ - impl_->highest_received_offset_;
}

auto quic_stream::bytes_received() const noexcept -> std::uint64_t
{
    return impl_->highest_received_offset_;
}

auto quic_stream::bytes_consumed() const noexcept -> std::uint64_t
{
    return impl_->next_expected_offset_;
}

auto quic_stream::bytes_sent() const noexcept -> std::uint64_t
{
    return impl_->total_sent_;
}

auto quic_stream::id() const noexcept -> stream_id
{
    return impl_->id_;
}

auto quic_stream::is_bidirectional() const noexcept -> bool
{
    return impl_->bidirectional_;
}

} // namespace cnetmod::quic

    #endif // CNETMOD_ENABLE_QUIC
#endif     // CNETMOD_HAS_SSL
