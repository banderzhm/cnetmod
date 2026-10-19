module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.quic.send_pipeline;

import std;

namespace cnetmod::quic {

quic_send_frame::quic_send_frame(std::vector<std::byte> bytes)
    : owned_bytes_(std::move(bytes))
{
}

auto priority_service_budget::take_next(const std::array<bool, 8>& ready)
    -> std::optional<std::uint8_t>
{
    for (unsigned epoch{}; epoch < 2U; ++epoch)
    {
        for (std::uint8_t urgency{}; urgency < credits_.size(); ++urgency)
        {
            if (ready[urgency] && credits_[urgency] != 0U)
            {
                --credits_[urgency];
                return urgency;
            }
        }
        if (!std::ranges::any_of(ready, [](bool value)
                {
                    return value;
                }))
            return std::nullopt;
        credits_ = quantum;
    }
    return std::nullopt;
}

void priority_service_budget::restore(std::uint8_t urgency) noexcept
{
    urgency = std::min<std::uint8_t>(urgency, 7U);
    credits_[urgency] = std::min<std::uint8_t>(quantum[urgency],
        static_cast<std::uint8_t>(credits_[urgency] + 1U));
}

void priority_service_budget::reset() noexcept
{
    credits_ = quantum;
}

auto quic_send_frame::owned(std::vector<std::byte> bytes,
    std::optional<std::uint64_t> stream) -> quic_send_frame
{
    quic_send_frame result;
    result.owned_bytes_ = std::move(bytes);
    result.stream_ = stream;
    return result;
}

auto quic_send_frame::shared_stream(std::span<const std::byte> header,
    std::shared_ptr<const std::string> body, std::size_t body_offset,
    std::size_t body_size, std::uint64_t stream) -> quic_send_frame
{
    if (!body || header.size() > quic_send_frame{}.stream_header_.size() ||
        body_offset > body->size() || body_size > body->size() - body_offset)
        std::terminate();
    quic_send_frame result;
    std::ranges::copy(header, result.stream_header_.begin());
    result.stream_header_size_ = header.size();
    result.shared_bytes_ = std::move(body);
    result.shared_offset_ = body_offset;
    result.shared_size_ = body_size;
    result.stream_ = stream;
    return result;
}

auto quic_send_frame::shared_stream_with_prefix(
    std::span<const std::byte> header, std::span<const std::byte> prefix,
    std::shared_ptr<const std::string> body, std::size_t body_offset,
    std::size_t body_size, std::uint64_t stream) -> quic_send_frame
{
    if (!body || header.empty() ||
        header.size() > quic_send_frame{}.stream_header_.size() ||
        body_offset > body->size() || body_size > body->size() - body_offset ||
        prefix.size() > std::numeric_limits<std::size_t>::max() - header.size() ||
        body_size > std::numeric_limits<std::size_t>::max() - header.size() - prefix.size())
        std::terminate();
    quic_send_frame result;
    result.owned_bytes_.reserve(header.size() + prefix.size());
    result.owned_bytes_.insert(result.owned_bytes_.end(), header.begin(), header.end());
    result.owned_bytes_.insert(result.owned_bytes_.end(), prefix.begin(), prefix.end());
    result.shared_bytes_ = std::move(body);
    result.shared_offset_ = body_offset;
    result.shared_size_ = body_size;
    result.stream_ = stream;
    return result;
}

auto quic_send_frame::empty() const noexcept -> bool
{
    return owned_bytes_.empty() && stream_header_size_ == 0U;
}

auto quic_send_frame::size() const noexcept -> std::size_t
{
    const auto owned_size = owned_bytes_.size();
    return shared_bytes_ ? owned_size + stream_header_size_ + shared_size_ : owned_size;
}

auto quic_send_frame::stream() const noexcept -> std::optional<std::uint64_t>
{
    return stream_;
}

void quic_send_frame::set_priority(std::uint8_t urgency, bool incremental) noexcept
{
    urgency_ = std::min<std::uint8_t>(urgency, 7U);
    incremental_ = incremental;
}

auto quic_send_frame::urgency() const noexcept -> std::uint8_t
{
    return urgency_;
}

auto quic_send_frame::incremental() const noexcept -> bool
{
    return incremental_;
}

void quic_send_frame::mark_early_data() noexcept { early_data_ = true; }
auto quic_send_frame::is_early_data() const noexcept -> bool { return early_data_; }

auto quic_send_frame::is_connection_close() const noexcept -> bool
{
    return !owned_bytes_.empty() &&
        (owned_bytes_.front() == std::byte{0x1c} ||
            owned_bytes_.front() == std::byte{0x1d}) &&
        !shared_bytes_;
}

auto quic_send_frame::owned_bytes() const noexcept -> const std::vector<std::byte>&
{
    if (!shared_bytes_)
        return owned_bytes_;
    // A shared frame has no complete owned encoding, including mixed prefixes.
    static const std::vector<std::byte> empty;
    return empty;
}

auto quic_send_frame::release_owned_bytes() noexcept -> std::vector<std::byte>
{
    if (shared_bytes_)
        return {};
    return std::move(owned_bytes_);
}

auto quic_send_frame::append_to(std::vector<std::byte>& output) const -> void
{
    if (!shared_bytes_)
    {
        output.insert(output.end(), owned_bytes_.begin(), owned_bytes_.end());
        return;
    }
    if (!owned_bytes_.empty())
        output.insert(output.end(), owned_bytes_.begin(), owned_bytes_.end());
    output.insert(output.end(), stream_header_.begin(),
        stream_header_.begin() + stream_header_size_);
    const auto body = std::as_bytes(std::span{
        shared_bytes_->data() + shared_offset_, shared_size_});
    output.insert(output.end(), body.begin(), body.end());
}

void quic_send_frame::clear() noexcept
{
    owned_bytes_.clear();
    shared_bytes_.reset();
    stream_header_size_ = 0U;
    shared_offset_ = 0U;
    shared_size_ = 0U;
    stream_.reset();
    early_data_ = false;
}

auto operator==(const quic_send_frame& lhs,
    const quic_send_frame& rhs) noexcept -> bool
{
    if (lhs.shared_bytes_ || rhs.shared_bytes_)
        return lhs.owned_bytes_ == rhs.owned_bytes_ &&
            lhs.shared_bytes_ == rhs.shared_bytes_ &&
            lhs.shared_offset_ == rhs.shared_offset_ &&
            lhs.shared_size_ == rhs.shared_size_ &&
            lhs.stream_ == rhs.stream_ &&
            lhs.stream_header_size_ == rhs.stream_header_size_ &&
            std::equal(lhs.stream_header_.begin(),
                lhs.stream_header_.begin() + lhs.stream_header_size_,
                rhs.stream_header_.begin());
    return lhs.owned_bytes_ == rhs.owned_bytes_;
}

class quic_stream_scheduler::implementation
{
public:
    struct priority
    {
        std::uint8_t urgency{3U};
        bool incremental{};
    };
    struct pending_write;
    using pending_list = std::list<pending_write>;
    struct pending_write
    {
        std::uint64_t stream{};
        std::uint64_t first_offset{};
        std::vector<std::byte> prefix;
        std::shared_ptr<const std::string> body;
        std::size_t cursor{};
        std::size_t chunk_end{};
        std::size_t legacy_budget{};
        bool fin{};
        bool fin_emitted{};
        priority service;
        quic_stream_scheduler::shared_stream_header_writer header_writer{};
        pending_list::iterator position;
        [[nodiscard]] auto total() const noexcept -> std::size_t
        {
            return prefix.size() + body->size();
        }
        struct emitted
        {
            quic_send_frame frame;
            std::size_t cursor{};
            std::size_t chunk_end{};
            bool complete{};
            bool chunk_complete{};
        };
        [[nodiscard]] auto prepare(std::size_t frame_budget,
            std::optional<std::size_t> credit_budget) const -> std::optional<emitted>
        {
            std::array<std::byte, 32> header{};
            auto chunk_limit = chunk_end;
            const auto offset = first_offset + cursor;
            // Standard STREAM callback contract: count zero has a one-byte LEN;
            // type/sid/offset widths are independent of count and FIN value.
            const auto zero_header = header_writer(header, stream, offset, 0U, false);
            if (zero_header == 0U)
                return std::nullopt;
            const auto fixed_header = zero_header - 1U;
            const auto maximal_count = [&](std::size_t remaining, std::size_t budget,
                                           std::size_t extra) -> std::size_t
            {
                constexpr std::array<std::uint64_t, 4> minima{0U, 64U, 16384U, 1073741824U};
                constexpr std::array<std::uint64_t, 4> maxima{63U, 16383U, 1073741823U,
                    4611686018427387903ULL};
                constexpr std::array<std::size_t, 4> widths{1U, 2U, 4U, 8U};
                std::size_t best{};
                for (std::size_t index{}; index < widths.size(); ++index)
                {
                    const auto overhead = fixed_header + extra + widths[index];
                    if (overhead > budget)
                        continue;
                    const auto count = std::min<std::uint64_t>(
                        std::min(remaining, budget - overhead), maxima[index]);
                    if (count >= minima[index])
                        best = std::max(best, static_cast<std::size_t>(count));
                }
                return best;
            };
            if (chunk_limit == cursor && cursor < total())
            {
                const auto count = maximal_count(total() - cursor, legacy_budget,
                    offset == 0U ? 1U : 0U);
                if (count == 0U)
                    return std::nullopt;
                chunk_limit = cursor + count;
            }
            const auto limit = credit_budget ? std::min(frame_budget, *credit_budget) : frame_budget;
            const auto count = maximal_count(chunk_limit - cursor, limit, 0U);
            const auto header_size = header_writer(header, stream, offset, count,
                fin && cursor + count == total());
            if (header_size == 0U || header_size > limit || count > limit - header_size)
                return std::nullopt;
            if (count == 0U && (cursor != total() || !fin || fin_emitted))
                return std::nullopt;
            quic_send_frame frame;
            const auto prefix_left = cursor < prefix.size() ? prefix.size() - cursor : 0U;
            const auto prefix_count = std::min(count, prefix_left);
            if (prefix_count == count)
            {
                std::vector<std::byte> encoded;
                encoded.reserve(header_size + count);
                encoded.insert(encoded.end(), header.begin(), header.begin() + header_size);
                if (count != 0U)
                    encoded.insert(encoded.end(), prefix.begin() + cursor,
                        prefix.begin() + cursor + count);
                // Match the existing owned-prefix fragment's wire strategy.
                frame = quic_send_frame{std::move(encoded)};
            }
            else if (prefix_count != 0U)
            {
                frame = quic_send_frame::shared_stream_with_prefix(
                    std::span{header}.first(header_size),
                    std::span<const std::byte>{prefix}.subspan(cursor, prefix_count),
                    body, 0U, count - prefix_count, stream);
            }
            else
            {
                frame = quic_send_frame::shared_stream(std::span{header}.first(header_size),
                    body, cursor - prefix.size(), count, stream);
            }
            frame.set_priority(service.urgency, service.incremental);
            return emitted{std::move(frame), cursor + count, chunk_limit,
                cursor + count == total(), cursor + count == chunk_limit};
        }
    };
    struct entry
    {
        std::variant<quic_send_frame, pending_write*> value;
        entry() = default;
        entry(quic_send_frame frame) : value(std::move(frame)) {}
        entry(pending_write* pending) : value(pending) {}
        [[nodiscard]] auto pending() const noexcept -> pending_write*
        {
            const auto pointer = std::get_if<pending_write*>(&value);
            return pointer ? *pointer : nullptr;
        }
        auto set_priority(std::uint8_t urgency, bool incremental) noexcept -> void
        {
            if (auto* write = pending())
                write->service = priority{urgency, incremental};
            else
                std::get<quic_send_frame>(value).set_priority(urgency, incremental);
        }
        [[nodiscard]] auto is_early_data() const noexcept -> bool
        {
            return !pending() && std::get<quic_send_frame>(value).is_early_data();
        }
        auto release_frame() -> quic_send_frame
        {
            return std::move(std::get<quic_send_frame>(value));
        }
    };
    struct scheduled_entry
    {
        std::uint64_t stream{};
        std::uint8_t urgency{};
        bool default_fifo{};
        entry frame;
    };
    struct reservation
    {
        priority_service_budget budget_before;
        std::uint64_t stream{};
        std::uint8_t urgency{};
        bool default_fifo{};
        bool popped{};
        bool still_ready{};
        pending_write* pending{};
        std::size_t cursor_before{};
        std::size_t chunk_before{};
        bool fin_before{};
        bool owner_complete{};
    };
    std::map<std::uint64_t, priority> priorities;
    std::deque<scheduled_entry> defaults;
    std::array<std::map<std::uint64_t, std::deque<entry>>, 8> frames;
    std::array<std::deque<std::uint64_t>, 8> ready;
    priority_service_budget budget;
    pending_list pending;
    std::uint64_t committed_pending_progress{};
    bool selection_active{};
    std::vector<reservation> reservations;
};

quic_stream_scheduler::quic_stream_scheduler()
    : impl_(std::make_unique<implementation>())
{
}

quic_stream_scheduler::~quic_stream_scheduler() = default;
quic_stream_scheduler::quic_stream_scheduler(quic_stream_scheduler&&) noexcept = default;
auto quic_stream_scheduler::operator=(quic_stream_scheduler&&) noexcept
    -> quic_stream_scheduler& = default;

auto quic_stream_scheduler::empty() const noexcept -> bool
{
    return impl_->defaults.empty() && std::ranges::none_of(impl_->ready, [](const auto& streams)
                                          {
                                              return !streams.empty();
                                          });
}

auto quic_stream_scheduler::size() const noexcept -> std::size_t
{
    std::size_t result{impl_->defaults.size()};
    for (const auto& bucket : impl_->frames)
        for (const auto& [_, frames] : bucket)
            result += frames.size();
    return result;
}

void quic_stream_scheduler::enqueue(std::uint64_t stream, quic_send_frame frame)
{
    const auto priority = impl_->priorities.find(stream);
    if (priority == impl_->priorities.end())
    {
        impl_->defaults.push_back({stream, 3U, true, std::move(frame)});
        return;
    }
    auto& frames = impl_->frames[priority->second.urgency][stream];
    frame.set_priority(priority->second.urgency, priority->second.incremental);
    if (frames.empty())
        impl_->ready[priority->second.urgency].push_back(stream);
    frames.push_back(std::move(frame));
}

auto quic_stream_scheduler::take() -> std::optional<scheduled_quic_frame>
{
    if (has_pending_writes())
    {
        begin_packet_selection();
        auto result = take_selected_frame(std::numeric_limits<std::size_t>::max(), std::nullopt);
        finish_packet_selection(true);
        return result;
    }
    for (;;)
    {
        std::array<bool, 8> ready{};
        for (std::size_t urgency{}; urgency < ready.size(); ++urgency)
            ready[urgency] = !impl_->ready[urgency].empty();
        ready[3] = ready[3] || !impl_->defaults.empty();
        const auto selected = impl_->budget.take_next(ready);
        if (!selected)
            return std::nullopt;
        const auto urgency = *selected;
        if (urgency == 3U && !impl_->defaults.empty())
        {
            auto frame = std::move(impl_->defaults.front());
            impl_->defaults.pop_front();
            return scheduled_quic_frame{frame.stream, frame.urgency,
                frame.default_fifo, frame.frame.release_frame()};
        }
        auto& streams = impl_->ready[urgency];
        while (!streams.empty())
        {
            const auto stream = streams.front();
            streams.pop_front();
            auto found = impl_->frames[urgency].find(stream);
            if (found == impl_->frames[urgency].end() || found->second.empty())
                continue;
            auto frame = found->second.front().release_frame();
            found->second.pop_front();
            if (!found->second.empty())
            {
                if (frame.incremental())
                    streams.push_back(stream);
                else
                    streams.push_front(stream);
            }
            else
            {
                impl_->frames[urgency].erase(found);
            }
            return scheduled_quic_frame{stream, urgency, false, std::move(frame)};
        }
        impl_->budget.restore(urgency);
    }
}

auto quic_stream_scheduler::pending_progress() const noexcept -> std::uint64_t
{
    return impl_->committed_pending_progress;
}

auto quic_stream_scheduler::has_pending_writes() const noexcept -> bool
{
    return !impl_->pending.empty();
}

void quic_stream_scheduler::enqueue_shared_write(std::uint64_t stream,
    std::vector<std::byte> prefix, std::shared_ptr<const std::string> body,
    std::uint64_t first_offset, bool fin, std::size_t legacy_budget,
    shared_stream_header_writer writer)
{
    const auto found = impl_->priorities.find(stream);
    const auto priority = found == impl_->priorities.end()
        ? implementation::priority{} : found->second;
    implementation::pending_write prepared;
    prepared.stream = stream;
    prepared.first_offset = first_offset;
    prepared.prefix = std::move(prefix);
    prepared.body = std::move(body);
    prepared.fin = fin;
    prepared.legacy_budget = legacy_budget;
    prepared.service = priority;
    prepared.header_writer = writer;
    impl_->pending.emplace_back(std::move(prepared));
    auto& pending = impl_->pending.back();
    pending.position = std::prev(impl_->pending.end());
    if (found == impl_->priorities.end())
        impl_->defaults.push_back({stream, 3U, true, &pending});
    else
    {
        auto& queue = impl_->frames[priority.urgency][stream];
        if (queue.empty())
            impl_->ready[priority.urgency].push_back(stream);
        queue.emplace_back(&pending);
    }
}

void quic_stream_scheduler::begin_packet_selection()
{
    if (impl_->selection_active)
        std::terminate();
    impl_->reservations.clear();
    // Reused journal capacity belongs to the scheduler, never sent frames.
    if (impl_->reservations.capacity() == 0U)
        impl_->reservations.reserve(16U);
    impl_->selection_active = true;
}

auto quic_stream_scheduler::take_selected_frame(std::size_t frame_budget,
    std::optional<std::size_t> pending_credit_budget)
    -> std::optional<scheduled_quic_frame>
{
    if (!impl_->selection_active)
        std::terminate();
    // Grow the reusable journal before taking credit or mutating queues.
    if (impl_->reservations.size() == impl_->reservations.capacity())
        impl_->reservations.reserve(std::max(std::size_t{16}, impl_->reservations.size() * 2U));
    for (;;)
    {
        std::array<bool, 8> ready{};
        for (std::size_t urgency{}; urgency < ready.size(); ++urgency)
            ready[urgency] = !impl_->ready[urgency].empty();
        ready[3] = ready[3] || !impl_->defaults.empty();
        const auto budget_before = impl_->budget;
        const auto selected = impl_->budget.take_next(ready);
        if (!selected)
            return std::nullopt;
        const auto urgency = *selected;
        const bool default_fifo = urgency == 3U && !impl_->defaults.empty();
        std::uint64_t stream{};
        implementation::entry* entry{};
        if (default_fifo)
        {
            stream = impl_->defaults.front().stream;
            entry = &impl_->defaults.front().frame;
        }
        else
        {
            auto& streams = impl_->ready[urgency];
            while (!streams.empty())
            {
                stream = streams.front();
                auto found = impl_->frames[urgency].find(stream);
                if (found != impl_->frames[urgency].end() && !found->second.empty())
                {
                    entry = &found->second.front();
                    break;
                }
                streams.pop_front();
            }
            if (!entry)
            {
                impl_->budget = budget_before;
                continue;
            }
        }
        implementation::reservation reservation;
        reservation.budget_before = budget_before;
        reservation.stream = stream;
        reservation.urgency = urgency;
        reservation.default_fifo = default_fifo;
        reservation.pending = entry->pending();
        quic_send_frame frame;
        bool chunk_complete = true;
        if (reservation.pending)
        {
            auto& pending = *reservation.pending;
            auto prepared = pending.prepare(frame_budget, pending_credit_budget);
            if (!prepared)
            {
                impl_->budget = budget_before;
                return std::nullopt;
            }
            reservation.cursor_before = pending.cursor;
            reservation.chunk_before = pending.chunk_end;
            reservation.fin_before = pending.fin_emitted;
            frame = std::move(prepared->frame);
            pending.cursor = prepared->cursor;
            pending.chunk_end = prepared->chunk_end;
            pending.fin_emitted = prepared->complete && pending.fin;
            reservation.owner_complete = prepared->complete;
            reservation.popped = prepared->complete;
            chunk_complete = prepared->chunk_complete;
        }
        else
        {
            frame = entry->release_frame();
            reservation.popped = true;
        }
        if (default_fifo)
        {
            if (reservation.popped)
                impl_->defaults.pop_front();
        }
        else
        {
            auto& streams = impl_->ready[urgency];
            streams.pop_front();
            auto found = impl_->frames[urgency].find(stream);
            auto& queue = found->second;
            if (reservation.popped)
                queue.pop_front();
            if (!queue.empty())
            {
                reservation.still_ready = true;
                // Preserve the original same-urgency logical chunk turn.
                // Global urgency credits were charged for this actual fragment.
                if (frame.incremental() && chunk_complete)
                    streams.push_back(stream);
                else
                    streams.push_front(stream);
            }
            else
                impl_->frames[urgency].erase(found);
        }
        impl_->reservations.push_back(reservation);
        return scheduled_quic_frame{stream, urgency, default_fifo, std::move(frame)};
    }
}

void quic_stream_scheduler::rollback_selected_frame(scheduled_quic_frame frame)
{
    if (!impl_->selection_active || impl_->reservations.empty())
        std::terminate();
    const auto reservation = impl_->reservations.back();
    impl_->reservations.pop_back();
    if (reservation.stream != frame.stream || reservation.urgency != frame.urgency ||
        reservation.default_fifo != frame.default_fifo)
        std::terminate();
    implementation::entry restored;
    if (reservation.pending)
    {
        auto& pending = *reservation.pending;
        pending.cursor = reservation.cursor_before;
        pending.chunk_end = reservation.chunk_before;
        pending.fin_emitted = reservation.fin_before;
        restored = implementation::entry{&pending};
    }
    else
        restored = implementation::entry{std::move(frame.frame)};
    if (reservation.default_fifo)
    {
        if (reservation.popped)
            impl_->defaults.push_front({frame.stream, frame.urgency, true, std::move(restored)});
    }
    else
    {
        auto& streams = impl_->ready[frame.urgency];
        if (reservation.still_ready)
            std::erase(streams, frame.stream);
        if (reservation.popped)
            impl_->frames[frame.urgency][frame.stream].push_front(std::move(restored));
        streams.push_front(frame.stream);
    }
    impl_->budget = reservation.budget_before;
}

void quic_stream_scheduler::finish_packet_selection(bool committed)
{
    if (!impl_->selection_active || (!committed && !impl_->reservations.empty()))
        std::terminate();
    if (committed)
        for (const auto& reservation : impl_->reservations)
        {
            if (reservation.pending)
                ++impl_->committed_pending_progress;
            if (reservation.owner_complete)
                impl_->pending.erase(reservation.pending->position);
        }
    impl_->reservations.clear();
    impl_->selection_active = false;
}

void quic_stream_scheduler::requeue(scheduled_quic_frame frame)
{
    impl_->budget.restore(frame.urgency);
    if (frame.default_fifo)
    {
        impl_->defaults.push_front({frame.stream, frame.urgency, true, std::move(frame.frame)});
        return;
    }
    auto& frames = impl_->frames[frame.urgency][frame.stream];
    if (frames.empty())
        impl_->ready[frame.urgency].push_front(frame.stream);
    frames.push_front(std::move(frame.frame));
}

auto quic_stream_scheduler::requeue_retransmission(quic_send_frame& frame) -> bool
{
    const auto stream = frame.stream();
    if (!stream)
        return false;
    const auto priority = impl_->priorities.find(*stream);
    if (priority != impl_->priorities.end())
        frame.set_priority(priority->second.urgency, priority->second.incremental);
    const auto urgency = frame.urgency();
    const bool default_fifo = urgency == 3U && !frame.incremental();
    requeue(scheduled_quic_frame{*stream, urgency, default_fifo, std::move(frame)});
    return true;
}

void quic_stream_scheduler::set_priority(std::uint64_t stream,
    std::uint8_t urgency, bool incremental) noexcept
{
    urgency = std::min<std::uint8_t>(urgency, 7U);
    const auto previous = impl_->priorities.find(stream);
    const auto old = previous == impl_->priorities.end()
        ? implementation::priority{}
        : previous->second;
    if (urgency == 3U && !incremental)
    {
        if (previous == impl_->priorities.end())
            return;
        impl_->priorities.erase(previous);
        auto pending = impl_->frames[old.urgency].find(stream);
        if (pending == impl_->frames[old.urgency].end())
            return;
        auto frames = std::move(pending->second);
        impl_->frames[old.urgency].erase(pending);
        std::erase(impl_->ready[old.urgency], stream);
        for (auto& frame : frames)
        {
            frame.set_priority(3U, false);
            impl_->defaults.push_back({stream, 3U, true, std::move(frame)});
        }
        return;
    }
    impl_->priorities.insert_or_assign(stream,
        implementation::priority{urgency, incremental});
    if (previous != impl_->priorities.end() && old.urgency == urgency)
    {
        if (const auto pending = impl_->frames[urgency].find(stream);
            pending != impl_->frames[urgency].end())
            for (auto& frame : pending->second)
                frame.set_priority(urgency, incremental);
        return;
    }
    std::deque<implementation::entry> moved;
    if (previous != impl_->priorities.end())
    {
        auto pending = impl_->frames[old.urgency].find(stream);
        if (pending == impl_->frames[old.urgency].end())
            return;
        moved = std::move(pending->second);
        impl_->frames[old.urgency].erase(pending);
        std::erase(impl_->ready[old.urgency], stream);
    }
    else
    {
        for (auto frame = impl_->defaults.begin(); frame != impl_->defaults.end();)
        {
            if (frame->stream != stream)
            {
                ++frame;
                continue;
            }
            moved.push_back(std::move(frame->frame));
            frame = impl_->defaults.erase(frame);
        }
        if (moved.empty())
            return;
    }
    auto& destination = impl_->frames[urgency][stream];
    for (auto& frame : moved)
        frame.set_priority(urgency, incremental);
    const bool was_empty = destination.empty();
    destination.insert(destination.end(), std::make_move_iterator(moved.begin()),
        std::make_move_iterator(moved.end()));
    if (was_empty && !destination.empty())
        impl_->ready[urgency].push_back(stream);
}

void quic_stream_scheduler::retire_stream(std::uint64_t stream) noexcept
{
    impl_->priorities.erase(stream);
}

void quic_stream_scheduler::discard_early_data() noexcept
{
    std::erase_if(impl_->defaults, [](const auto& frame) { return frame.frame.is_early_data(); });
    for (std::size_t urgency{}; urgency < impl_->frames.size(); ++urgency)
    {
        auto& streams = impl_->frames[urgency];
        for (auto frame = streams.begin(); frame != streams.end();)
        {
            std::erase_if(frame->second, [](const auto& value) { return value.is_early_data(); });
            if (frame->second.empty())
            {
                std::erase(impl_->ready[urgency], frame->first);
                frame = streams.erase(frame);
            }
            else
                ++frame;
        }
    }
}

auto quic_stream_scheduler::take_early_data() -> std::vector<quic_send_frame>
{
    std::vector<quic_send_frame> result;
    const auto take = [&result](auto& frame)
    {
        if (!frame.is_early_data())
            return false;
        result.push_back(frame.release_frame());
        return true;
    };
    std::erase_if(impl_->defaults, [&](auto& value) { return take(value.frame); });
    for (auto& streams : impl_->frames)
        for (auto& [_, frames] : streams)
            std::erase_if(frames, take);
    // Empty buckets/ready entries are removed by the shared discard pass.
    discard_early_data();
    return result;
}

auto quic_stream_scheduler::priority_count() const noexcept -> std::size_t
{
    return impl_->priorities.size();
}

} // namespace cnetmod::quic
