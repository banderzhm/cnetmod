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

auto quic_send_frame::empty() const noexcept -> bool
{
    return owned_bytes_.empty() && stream_header_size_ == 0U;
}

auto quic_send_frame::size() const noexcept -> std::size_t
{
    return owned_bytes_.empty() ? stream_header_size_ + shared_size_
                                : owned_bytes_.size();
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
            owned_bytes_.front() == std::byte{0x1d});
}

auto quic_send_frame::owned_bytes() const noexcept -> const std::vector<std::byte>&
{
    return owned_bytes_;
}

auto quic_send_frame::release_owned_bytes() noexcept -> std::vector<std::byte>
{
    return std::move(owned_bytes_);
}

auto quic_send_frame::append_to(std::vector<std::byte>& output) const -> void
{
    if (!owned_bytes_.empty())
    {
        output.insert(output.end(), owned_bytes_.begin(), owned_bytes_.end());
        return;
    }
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
        return lhs.shared_bytes_ == rhs.shared_bytes_ &&
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

    std::map<std::uint64_t, priority> priorities;
    std::deque<scheduled_quic_frame> defaults;
    std::array<std::map<std::uint64_t, std::deque<quic_send_frame>>, 8> frames;
    std::array<std::deque<std::uint64_t>, 8> ready;
    priority_service_budget budget;
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
            return frame;
        }
        auto& streams = impl_->ready[urgency];
        while (!streams.empty())
        {
            const auto stream = streams.front();
            streams.pop_front();
            auto found = impl_->frames[urgency].find(stream);
            if (found == impl_->frames[urgency].end() || found->second.empty())
                continue;
            auto frame = std::move(found->second.front());
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

void quic_stream_scheduler::requeue(scheduled_quic_frame frame)
{
    impl_->budget.restore(frame.urgency);
    if (frame.default_fifo)
    {
        impl_->defaults.push_front(std::move(frame));
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
    std::deque<quic_send_frame> moved;
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
        result.push_back(std::move(frame));
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
