module cnetmod.protocol.http.v2.session;

import std;
import cnetmod.utils.concurrent_containers.atomic_rw_latch;
import cnetmod.core.buffer;
import cnetmod.core.socket;
import cnetmod.coro.task;
import cnetmod.coro.task_group;
import cnetmod.coro.mutex;
import cnetmod.coro.spawn;
import cnetmod.coro.timer;
import cnetmod.protocol.http.v2.flow_control;
import cnetmod.executor.async_op;
import cnetmod.io.io_context;

namespace cnetmod::http::v2 {
namespace {
    constexpr std::string_view client_preface = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
    constexpr std::uint8_t flag_ack = 0x1;
    constexpr std::uint8_t flag_end_stream = 0x1;
    constexpr std::uint8_t flag_end_headers = 0x4;
    constexpr std::uint8_t flag_padded = 0x8;
    constexpr std::uint8_t flag_priority = 0x20;
    constexpr std::uint32_t error_protocol = 0x1;
    constexpr std::uint32_t error_frame_size = 0x6;
    constexpr std::size_t response_batch_bytes = 64U * 1024U;
    // DATA payloads at least this large are framed by reference when the
    // transport supports vectored writes; smaller ones are cheaper to copy.
    constexpr std::size_t borrowed_frame_threshold = 4U * 1024U;
    constexpr auto response_batch_max_age = std::chrono::microseconds{250};
    constexpr std::size_t response_batch_time_check_interval = 32;

    auto read_u32(std::span<const std::byte> value) -> std::uint32_t
    {
        return (std::to_integer<std::uint32_t>(value[0]) << 24) |
            (std::to_integer<std::uint32_t>(value[1]) << 16) |
            (std::to_integer<std::uint32_t>(value[2]) << 8) |
            std::to_integer<std::uint32_t>(value[3]);
    }

} // namespace

session::session(cnetmod::io_context& context, cnetmod::socket& socket,
    server_handler handler, transport_reader reader,
    transport_writer writer)
    : context_(&context), socket_(&socket), reader_(std::move(reader)), writer_(std::move(writer)), streams_(&stream_pool_), handler_(std::move(handler))
{
    // Avoid both ends of the allocation trade-off: the common case should not
    // grow on its first multiplexed batch, while thousands of mostly-idle H2
    // connections must not each reserve the peer's full 100-stream limit.
    const auto initial_stream_capacity = std::min<std::uint32_t>(
        local_.max_concurrent_streams, 32U);
    streams_.reserve(initial_stream_capacity);
    ready_requests_.reserve(initial_stream_capacity);
    input_.reserve(16U * 1024U);
    outbound_.reserve(4U * 1024U);
    write_buffer_.reserve(4U * 1024U);
    header_block_.reserve(4U * 1024U);
    response_headers_.reserve(8);
    encoded_headers_.reserve(256);
    if (!reader_)
    {
        reader_ = [this](cnetmod::mutable_buffer buffer)
            -> cnetmod::task<std::expected<std::size_t, std::error_code>>
        {
            co_return co_await cnetmod::async_read(*context_, *socket_, buffer);
        };
    }
    if (!writer_)
    {
        writer_ = [this](cnetmod::const_buffer buffer)
            -> cnetmod::task<std::expected<void, std::error_code>>
        {
            co_return co_await cnetmod::async_write_all(*context_, *socket_, buffer);
        };
    }
}

session::session(cnetmod::io_context& context, cnetmod::socket& socket,
    streaming_server_handler handler,
    cnetmod::http::request_body_stream_options body_options,
    transport_reader reader, transport_writer writer)
    : session(context, socket, server_handler{}, std::move(reader),
          std::move(writer))
{
    if (!handler || body_options.max_bytes == 0 ||
        body_options.chunk_capacity < 4)
        throw std::invalid_argument(
            "HTTP/2 streaming requires a handler, positive limit, and at least four body slots");
    streaming_handler_ = std::move(handler);
    body_options_ = body_options;
    background_ = std::make_unique<cnetmod::task_group>(context);
}

session::session(cnetmod::io_context& context, cnetmod::socket& socket,
    borrowed_streaming_server_handler handler,
    cnetmod::http::request_body_stream_options body_options,
    transport_reader reader, transport_writer writer,
    sync_server_handler sync_handler, bool buffer_complete_requests,
    buffered_request_filter buffer_filter)
    : session(context, socket, server_handler{}, std::move(reader),
          std::move(writer))
{
    if (!handler || body_options.max_bytes == 0 ||
        body_options.chunk_capacity < 4)
        throw std::invalid_argument(
            "HTTP/2 streaming requires a handler, positive limit, and at least four body slots");
    borrowed_streaming_handler_ = std::move(handler);
    sync_handler_ = std::move(sync_handler);
    buffer_complete_requests_ = buffer_complete_requests;
    buffer_filter_ = std::move(buffer_filter);
    body_options_ = body_options;
    background_ = std::make_unique<cnetmod::task_group>(context);
    request_header_pool_.reserve(std::min<std::uint32_t>(
        local_.max_concurrent_streams, 32U));
}

auto session::acquire_request_headers() -> std::vector<header_field>
{
    if (request_header_pool_.empty())
        return {};
    auto headers = std::move(request_header_pool_.back());
    request_header_pool_.pop_back();
    return headers;
}

void session::release_request_headers(
    std::vector<header_field> headers) noexcept
{
    // Reusing capacity must not turn the connection-local pool into a store of
    // credentials.  HPACK's sensitive flag is advisory and clients do not
    // consistently set it, so also recognize the standard credential fields.
    for (auto& header : headers)
    {
        if (!header.sensitive && header.name != "authorization" &&
            header.name != "proxy-authorization" &&
            header.name != "cookie")
            continue;
        std::fill(header.value.begin(), header.value.end(), '\0');
        header.value.clear();
    }
    request_header_pool_.push_back(std::move(headers));
}

auto session::peer_settings() const noexcept -> const settings&
{
    return peer_;
}

auto session::local_settings() noexcept -> settings&
{
    return local_;
}

void session::set_gather_writer(transport_gather_writer writer)
{
    gather_writer_ = std::move(writer);
}

auto session::take_outbound() -> std::vector<std::byte>
{
    if (outbound_borrowed_.empty())
        return std::exchange(outbound_, {});
    std::vector<std::byte> joined;
    joined.reserve(pending_outbound_bytes());
    std::size_t cursor = 0;
    for (const auto& segment : outbound_borrowed_)
    {
        joined.insert(joined.end(), outbound_.begin() + static_cast<std::ptrdiff_t>(cursor),
            outbound_.begin() + static_cast<std::ptrdiff_t>(segment.offset));
        joined.insert(joined.end(), segment.data.begin(), segment.data.end());
        cursor = segment.offset;
    }
    joined.insert(joined.end(), outbound_.begin() + static_cast<std::ptrdiff_t>(cursor),
        outbound_.end());
    outbound_.clear();
    outbound_borrowed_.clear();
    outbound_borrowed_bytes_ = 0;
    return joined;
}

auto session::pending_outbound_bytes() const noexcept -> std::size_t
{
    return outbound_.size() + outbound_borrowed_bytes_;
}

void session::queue_frame(frame_header header,
    std::span<const std::byte> payload)
{
    header.length = static_cast<std::uint32_t>(payload.size());
    const auto encoded = encode_frame_header(header);
    outbound_.insert(outbound_.end(), encoded.begin(), encoded.end());
    outbound_.insert(outbound_.end(), payload.begin(), payload.end());
}

void session::queue_borrowed_frame(frame_header header,
    std::span<const std::byte> payload,
    const std::shared_ptr<const std::string>& owner)
{
    header.length = static_cast<std::uint32_t>(payload.size());
    const auto encoded = encode_frame_header(header);
    outbound_.insert(outbound_.end(), encoded.begin(), encoded.end());
    outbound_borrowed_.push_back({outbound_.size(), payload, owner});
    outbound_borrowed_bytes_ += payload.size();
}

void session::queue_connection_error(std::uint32_t code)
{
    std::array<std::byte, 8> payload{};
    const auto id = last_peer_stream_id_ & 0x7fff'ffffU;
    payload[0] = static_cast<std::byte>(id >> 24);
    payload[1] = static_cast<std::byte>(id >> 16);
    payload[2] = static_cast<std::byte>(id >> 8);
    payload[3] = static_cast<std::byte>(id);
    payload[4] = static_cast<std::byte>(code >> 24);
    payload[5] = static_cast<std::byte>(code >> 16);
    payload[6] = static_cast<std::byte>(code >> 8);
    payload[7] = static_cast<std::byte>(code);
    queue_frame({.type = frame_type::goaway}, payload);
    goaway_received_ = true;
}

auto session::validate_headers(std::span<const header_field> fields) const
    -> std::error_code
{
    bool regular_seen = false;
    bool method = false;
    bool scheme = false;
    bool path = false;
    for (const auto& field : fields)
    {
        if (field.name.empty())
            return std::make_error_code(std::errc::protocol_error);
        for (const auto ch : field.name)
            if (ch >= 'A' && ch <= 'Z')
                return std::make_error_code(std::errc::protocol_error);
        if (field.name.starts_with(':'))
        {
            if (regular_seen)
                return std::make_error_code(std::errc::protocol_error);
            method |= field.name == ":method";
            scheme |= field.name == ":scheme";
            path |= field.name == ":path";
        }
        else
        {
            regular_seen = true;
            if (field.name == "connection" || field.name == "keep-alive" ||
                field.name == "proxy-connection" ||
                field.name == "transfer-encoding" || field.name == "upgrade")
                return std::make_error_code(std::errc::protocol_error);
        }
    }
    return method && scheme && path
        ? std::error_code{}
        : std::make_error_code(std::errc::protocol_error);
}

void session::queue_response(std::uint32_t stream_id, server_response response, bool detached)
{
    if (!detached && !streams_.contains(stream_id))
        return;
    // Dynamic responses normally arrive as an owning string. Promote a large
    // one to a stable owner without copying its allocation so the DATA frames
    // can use the same gather path as cached static bodies. The owner remains
    // attached to a pending response or to the in-flight borrowed segments.
    if (gather_writer_ && !response.shared_body && response.body.empty() &&
        response.body_text.size() >= borrowed_frame_threshold)
    {
        response.shared_body =
            std::make_shared<const std::string>(std::move(response.body_text));
        response.body_text.clear();
    }
    const auto body = response.shared_body
        ? std::as_bytes(std::span{*response.shared_body})
        : response.body_text.empty()
        ? std::span<const std::byte>{response.body}
        : std::as_bytes(std::span{response.body_text});
    std::span<const std::byte> header_block;
    const auto use_preencoded = response.preencoded_headers != nullptr &&
        static_cast<int>(response.status) == response.preencoded_headers->status &&
        response.headers.empty() && response.static_headers.empty() &&
        response.body_source == nullptr;
    if (use_preencoded)
    {
        // A cached static response reuses its completely encoded header block,
        // skipping the status string, field vector and HPACK encoding.
        header_block = response.preencoded_headers->block;
    }
    else
    {
        response_headers_.clear();
        response_headers_.reserve(response.headers.size() + response.static_headers.fields().size() + 2);
        response_headers_.push_back({":status", std::to_string(response.status)});
        bool has_length = false;
        const auto append = [&](auto&& header)
        {
            if (header.name.starts_with(':') || header.name == "connection" ||
                header.name == "transfer-encoding" || header.name == "upgrade")
                return;
            has_length |= header.name == "content-length";
            response_headers_.push_back(std::forward<decltype(header)>(header));
        };
        for (auto& header : response.headers)
            append(std::move(header));
        for (const auto& header : response.static_headers.fields())
            append(header);
        if (response.automatic_content_length && !body.empty() && !has_length)
            response_headers_.push_back(
                {"content-length", std::to_string(body.size())});
        auto encoded = encoder_.encode(response_headers_, encoded_headers_);
        if (!encoded)
            return;
        header_block = encoded_headers_;
    }
    const auto has_trailers = !response.trailers.empty() || !response.static_trailers.empty();
    const auto end_on_headers = body.empty() && !has_trailers;
    queue_frame({.type = frame_type::headers,
                    .flags = static_cast<std::uint8_t>(
                        flag_end_headers | (end_on_headers ? flag_end_stream : 0)),
                    .stream_id = stream_id},
        header_block);
    std::size_t offset{};
    if (queue_response_data(stream_id, response, offset, detached))
        streams_.erase(stream_id);
    else
    {
        auto [found, inserted] = streams_.try_emplace(stream_id, stream_id,
            static_cast<std::int32_t>(peer_.initial_window_size),
            static_cast<std::int32_t>(local_.initial_window_size), std::vector<header_field>{});
        if (inserted)
        {
            (void)found->second.receive_headers({}, true);
            (void)found->second.send_window().consume(static_cast<std::uint32_t>(offset));
        }
        pending_responses_.emplace(stream_id, pending_response{std::move(response), offset});
    }
}

auto session::queue_response_data(std::uint32_t stream_id, server_response& response,
    std::size_t& offset, bool detached) -> bool
{
    const auto found = streams_.find(stream_id);
    if (found == streams_.end() && !detached)
        return true;
    const auto body = response.shared_body
        ? std::as_bytes(std::span{*response.shared_body})
        : response.body_text.empty()
        ? std::span<const std::byte>{response.body}
        : std::as_bytes(std::span{response.body_text});
    const bool trailers = !response.trailers.empty() || !response.static_trailers.empty();
    while (offset < body.size())
    {
        const auto stream_window = found == streams_.end()
            ? static_cast<std::int64_t>(peer_.initial_window_size) - static_cast<std::int64_t>(offset)
            : static_cast<std::int64_t>(found->second.send_window().available());
        const auto available = std::min<std::int64_t>(send_window_, stream_window);
        if (available <= 0)
            return false;
        const auto size = std::min({static_cast<std::size_t>(peer_.max_frame_size),
            body.size() - offset, static_cast<std::size_t>(available)});
        send_window_ -= static_cast<std::int32_t>(size);
        if (found != streams_.end())
            (void)found->second.send_window().consume(static_cast<std::uint32_t>(size));
        const frame_header data_header{.type = frame_type::data,
            .flags = static_cast<std::uint8_t>(offset + size == body.size() && !trailers ? flag_end_stream : 0),
            .stream_id = stream_id};
        // Shared bodies (the static-file cache) outlive this response, so a
        // vectored transport can send them by reference without a copy.
        if (gather_writer_ && response.shared_body && size >= borrowed_frame_threshold)
            queue_borrowed_frame(data_header, body.subspan(offset, size), response.shared_body);
        else
            queue_frame(data_header, body.subspan(offset, size));
        offset += size;
    }
    if (trailers && !queue_response_trailers(stream_id, response))
        queue_connection_error(error_protocol);
    return true;
}

auto session::queue_response_trailers(std::uint32_t stream_id, const server_response& response)
    -> std::expected<void, std::error_code>
{
    std::span<const header_field> fields = response.trailers;
    if (!response.static_trailers.empty())
    {
        if (fields.empty())
            fields = response.static_trailers.fields();
        else
        {
            response_trailers_.clear();
            response_trailers_.reserve(fields.size() + response.static_trailers.fields().size());
            response_trailers_.insert(response_trailers_.end(), fields.begin(), fields.end());
            const auto fixed = response.static_trailers.fields();
            response_trailers_.insert(response_trailers_.end(), fixed.begin(), fixed.end());
            fields = response_trailers_;
        }
    }
    auto encoded = encoder_.encode(fields, encoded_headers_);
    if (!encoded)
        return std::unexpected(encoded.error());
    queue_frame({.type = frame_type::headers,
                    .flags = static_cast<std::uint8_t>(flag_end_headers | flag_end_stream),
                    .stream_id = stream_id},
        encoded_headers_);
    return {};
}

void session::drain_pending_responses()
{
    for (auto it = pending_responses_.begin(); it != pending_responses_.end();)
    {
        if (queue_response_data(it->first, it->second.response, it->second.offset))
        {
            streams_.erase(it->first);
            it = pending_responses_.erase(it);
        }
        else
            ++it;
    }
}

auto session::flush_outbound()
    -> cnetmod::task<std::expected<void, std::error_code>>
{
    co_await writer_mutex_.lock();
    cnetmod::async_lock_guard guard{writer_mutex_, std::adopt_lock};
    co_return co_await write_outbound_locked();
}

auto session::flush_batched_outbound()
    -> cnetmod::task<std::expected<void, std::error_code>>
{
    if (outbound_.empty())
        co_return std::expected<void, std::error_code>{};
    if (!writer_active_)
    {
        writer_active_ = true;
        const bool started = background_->run([this](cnetmod::cancel_token& cancellation)
                                                  -> cnetmod::task<std::expected<void, std::error_code>>
            {
                struct completion
                {
                    session& owner;
                    bool success{};
                    ~completion()
                    {
                        owner.writer_active_ = false;
                        if (!success)
                            owner.socket_->shutdown_both();
                    }
                } finished{*this};
                while (!outbound_.empty())
                {
                    if (cancellation.is_cancelled())
                        co_return std::unexpected(std::make_error_code(std::errc::operation_canceled));
                    auto written = co_await flush_outbound();
                    if (!written)
                        co_return written;
                }
                finished.success = true;
                co_return std::expected<void, std::error_code>{};
            });
        if (!started)
        {
            writer_active_ = false;
            co_return std::unexpected(std::make_error_code(std::errc::operation_canceled));
        }
    }
    // One buffer can be in the kernel while the reader builds the next batch.
    // Apply backpressure instead of accumulating an unbounded outbound queue.
    const auto in_flight = write_buffer_.size() +
        std::accumulate(write_borrowed_.begin(), write_borrowed_.end(), std::size_t{},
            [](std::size_t total, const borrowed_segment& segment)
            {
                return total + segment.data.size();
            });
    if (pending_outbound_bytes() + in_flight >= response_batch_bytes * 2)
        co_return co_await flush_outbound();
    co_return std::expected<void, std::error_code>{};
}

auto session::write_outbound_locked()
    -> cnetmod::task<std::expected<void, std::error_code>>
{
    if (outbound_.empty())
        co_return std::expected<void, std::error_code>{};
    write_buffer_.clear();
    write_buffer_.swap(outbound_);
    write_borrowed_.clear();
    write_borrowed_.swap(outbound_borrowed_);
    outbound_borrowed_bytes_ = 0;
    std::expected<void, std::error_code> result;
    if (write_borrowed_.empty())
        result = co_await writer_({write_buffer_.data(), write_buffer_.size()});
    else
    {
        // Interleave owned frame bytes with the borrowed payloads they frame.
        write_parts_.clear();
        std::size_t cursor = 0;
        for (const auto& segment : write_borrowed_)
        {
            if (segment.offset > cursor)
                write_parts_.push_back({write_buffer_.data() + cursor, segment.offset - cursor});
            write_parts_.push_back({segment.data.data(), segment.data.size()});
            cursor = segment.offset;
        }
        if (cursor < write_buffer_.size())
            write_parts_.push_back({write_buffer_.data() + cursor, write_buffer_.size() - cursor});
        result = co_await gather_writer_(write_parts_);
    }
    write_buffer_.clear();
    // Release the shared bodies only after the transport is done with them.
    write_borrowed_.clear();
    if (outbound_.empty())
    {
        outbound_.swap(write_buffer_);
        outbound_borrowed_.swap(write_borrowed_);
    }
    co_return result;
}

auto session::start_response_write(std::uint32_t stream_id, server_response response) -> bool
{
    return background_->run([this, stream_id, response = std::move(response)](
                                cnetmod::cancel_token& token) mutable
                                -> cnetmod::task<std::expected<void, std::error_code>>
        {
            co_return co_await write_response(stream_id, std::move(response), token);
        });
}

auto session::write_response(std::uint32_t stream_id,
    server_response response, cnetmod::cancel_token& cancellation)
    -> cnetmod::task<std::expected<void, std::error_code>>
{
    // The handler usually runs on this connection's own loop.  Posting
    // unconditionally would add a reactor round-trip to every response; only
    // hop when a caller from another executor is possible.
    if (!context_->running_in_this_thread())
        co_await cnetmod::post_awaitable{*context_};
    if (!streams_.contains(stream_id))
        co_return std::expected<void, std::error_code>{};
    if (response.body_source)
    {
        if (!response.body.empty() || !response.body_text.empty() ||
            response.shared_body)
            co_return std::unexpected(
                std::make_error_code(std::errc::invalid_argument));
        std::vector<header_field> headers;
        headers.reserve(response.headers.size() + response.static_headers.fields().size() + 2);
        headers.push_back({":status", std::to_string(response.status)});
        bool has_length = false;
        const auto append = [&](auto&& header)
        {
            if (header.name.starts_with(':') || header.name == "connection" ||
                header.name == "transfer-encoding" || header.name == "upgrade")
                return;
            has_length |= header.name == "content-length";
            headers.push_back(std::forward<decltype(header)>(header));
        };
        for (auto& header : response.headers)
            append(std::move(header));
        for (const auto& header : response.static_headers.fields())
            append(header);
        const auto expected_length = response.body_source->content_length();
        if (response.automatic_content_length && expected_length && !has_length)
            headers.push_back(
                {"content-length", std::to_string(*expected_length)});
        auto encoded = encoder_.encode(headers);
        if (!encoded)
            co_return std::unexpected(encoded.error());
        {
            co_await writer_mutex_.lock();
            cnetmod::async_lock_guard guard{writer_mutex_, std::adopt_lock};
            queue_frame({.type = frame_type::headers,
                            .flags = flag_end_headers,
                            .stream_id = stream_id},
                *encoded);
            auto written = co_await write_outbound_locked();
            if (!written)
                co_return written;
        }
        std::uint64_t total = 0;
        while (auto chunk = co_await response.body_source->next(cancellation))
        {
            std::size_t offset = 0;
            while (offset < chunk->size())
            {
                if (cancellation.is_cancelled() || goaway_received_)
                    co_return std::unexpected(std::make_error_code(std::errc::operation_canceled));
                const auto current = streams_.find(stream_id);
                if (current == streams_.end())
                    co_return std::expected<void, std::error_code>{};
                if (send_window_ <= 0 || current->second.send_window().available() <= 0)
                {
                    auto waited = co_await cnetmod::async_timer_wait(*context_,
                        std::chrono::milliseconds{1}, cancellation);
                    if (!waited)
                        co_return std::unexpected(waited.error());
                    continue;
                }
                co_await writer_mutex_.lock();
                cnetmod::async_lock_guard guard{writer_mutex_, std::adopt_lock};
                const auto found = streams_.find(stream_id);
                if (found == streams_.end())
                    co_return std::expected<void, std::error_code>{};
                const auto available = std::min(send_window_, found->second.send_window().available());
                if (available <= 0)
                    continue;
                const auto size = std::min({static_cast<std::size_t>(peer_.max_frame_size),
                    chunk->size() - offset, static_cast<std::size_t>(available)});
                if (expected_length &&
                    (total > *expected_length ||
                        size > *expected_length - total))
                    co_return std::unexpected(
                        std::make_error_code(std::errc::message_size));
                send_window_ -= static_cast<std::int32_t>(size);
                (void)found->second.send_window().consume(static_cast<std::uint32_t>(size));
                queue_frame({.type = frame_type::data,
                                .stream_id = stream_id},
                    {chunk->data() + offset, size});
                auto written = co_await write_outbound_locked();
                if (!written)
                    co_return written;
                offset += size;
                total += size;
            }
        }
        if (expected_length && total != *expected_length)
            co_return std::unexpected(
                std::make_error_code(std::errc::message_size));
        {
            co_await writer_mutex_.lock();
            cnetmod::async_lock_guard guard{writer_mutex_, std::adopt_lock};
            if (response.trailers.empty() && response.static_trailers.empty())
                queue_frame({.type = frame_type::data,
                                .flags = flag_end_stream,
                                .stream_id = stream_id},
                    {});
            else
            {
                auto trailers = queue_response_trailers(stream_id, response);
                if (!trailers)
                    co_return std::unexpected(trailers.error());
            }
            auto written = co_await write_outbound_locked();
            if (!written)
                co_return written;
        }
        streams_.erase(stream_id);
        co_return std::expected<void, std::error_code>{};
    }
    co_await writer_mutex_.lock();
    cnetmod::async_lock_guard guard{writer_mutex_, std::adopt_lock};
    queue_response(stream_id, std::move(response));
    co_return co_await write_outbound_locked();
}

void session::schedule_flow_credit(std::uint32_t stream_id, std::size_t bytes)
{
    if (bytes == 0)
        return;
    {
        cnetmod::concurrent_containers::exclusive_latch_guard lock{flow_credit_latch_};
        pending_flow_credit_.emplace_back(stream_id, bytes);
        if (flow_credit_scheduled_)
            return;
        flow_credit_scheduled_ = true;
    }
    (void)background_->run([this](cnetmod::cancel_token&)
                               -> cnetmod::task<std::expected<void, std::error_code>>
        {
            co_return co_await return_flow_credit();
        });
}

auto session::return_flow_credit()
    -> cnetmod::task<std::expected<void, std::error_code>>
{
    // Defer to the owner loop once, allowing consumers in this turn to share
    // one task and transport write. The short queue latch also permits a
    // consumer on another loop, just as the previous task_group::run path did.
    co_await cnetmod::post_awaitable{*context_};
    co_await writer_mutex_.lock();
    cnetmod::async_lock_guard guard{writer_mutex_, std::adopt_lock};
    std::vector<std::pair<std::uint32_t, std::size_t>> credits;
    {
        cnetmod::concurrent_containers::exclusive_latch_guard lock{flow_credit_latch_};
        credits.swap(pending_flow_credit_);
        flow_credit_scheduled_ = false;
    }
    std::uint32_t connection_credit{};
    for (const auto& [stream_id, bytes] : credits)
    {
        if (bytes > 0x7fff'ffffU - connection_credit)
            co_return std::unexpected(
                std::make_error_code(std::errc::invalid_argument));
        auto found = streams_.find(stream_id);
        if (found != streams_.end())
        {
            if (!found->second.receive_window().increase(
                    static_cast<std::uint32_t>(bytes)))
                co_return std::unexpected(
                    std::make_error_code(std::errc::operation_canceled));
            std::array<std::byte, 4> update{
                static_cast<std::byte>(bytes >> 24),
                static_cast<std::byte>(bytes >> 16),
                static_cast<std::byte>(bytes >> 8), static_cast<std::byte>(bytes)};
            queue_frame({.type = frame_type::window_update, .stream_id = stream_id}, update);
        }
        connection_credit += static_cast<std::uint32_t>(bytes);
    }
    if (connection_credit > static_cast<std::uint32_t>(
                                std::numeric_limits<std::int32_t>::max() - receive_window_))
        co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
    receive_window_ += static_cast<std::int32_t>(connection_credit);
    std::array<std::byte, 4> update{
        static_cast<std::byte>(connection_credit >> 24),
        static_cast<std::byte>(connection_credit >> 16),
        static_cast<std::byte>(connection_credit >> 8), static_cast<std::byte>(connection_credit)};
    queue_frame({.type = frame_type::window_update}, update);
    credits.clear();
    {
        cnetmod::concurrent_containers::exclusive_latch_guard lock{flow_credit_latch_};
        if (pending_flow_credit_.empty())
            pending_flow_credit_.swap(credits);
    }
    co_return co_await write_outbound_locked();
}

void session::start_streaming_request(std::uint32_t stream_id)
{
    auto found = streams_.find(stream_id);
    if (found == streams_.end() || !background_)
        return;
    auto body = std::make_shared<cnetmod::http::request_body_stream>(
        body_options_.chunk_capacity, body_options_.max_bytes);
    found->second.attach_body_stream(body);
    body->observe_consumption([this, stream_id](std::size_t bytes)
        {
            schedule_flow_credit(stream_id, bytes);
        });
    auto prefix = found->second.take_body();
    if (!prefix.empty())
        (void)body->push(cnetmod::http::request_body_chunk{prefix.begin(), prefix.end()});
    server_request request{.stream_id = stream_id,
        .headers = found->second.take_headers(),
        .body_stream = body};
    (void)background_->run(
        [this, stream_id, request = std::move(request)](cnetmod::cancel_token& token) mutable
            -> cnetmod::task<std::expected<void, std::error_code>>
        {
            server_response response;
            if (borrowed_streaming_handler_)
            {
                response = co_await borrowed_streaming_handler_(request, token);
                release_request_headers(std::move(request.headers));
            }
            else
                response = co_await streaming_handler_(std::move(request), token);
            co_return co_await write_response(
                stream_id, std::move(response), token);
        });
}

void session::close_streaming_bodies() noexcept
{
    for (auto& [id, value] : streams_)
    {
        (void)id;
        if (value.body_stream())
            value.body_stream()->close();
    }
}

auto session::shutdown_streaming() -> cnetmod::task<void>
{
    close_streaming_bodies();
    if (background_)
    {
        background_->cancel();
        co_await background_->settle();
    }
}

auto session::dispatch_ready() -> cnetmod::task<void>
{
    std::vector<ready_request> ready;
    ready.swap(ready_requests_);
    auto batch_started = std::chrono::steady_clock::now();
    std::size_t batched_responses{};
    std::uint32_t consumed_connection_credit{};
    for (auto& pending : ready)
    {
        const auto stream_id = pending.stream_id;
        const auto found = pending.detached
            ? streams_.end()
            : streams_.find(stream_id);
        if (!pending.detached && found == streams_.end())
            continue;
        if (borrowed_streaming_handler_ && !pending.detached &&
            !found->second.body().empty())
        {
            server_request request{.stream_id = stream_id,
                .headers = found->second.take_headers(),
                .body = found->second.take_body()};
            cancel_token cancellation;
            if (auto immediate = sync_handler_
                    ? sync_handler_(request, cancellation)
                    : std::nullopt)
            {
                // These complete requests have already received END_STREAM.
                // Their synchronous consumer has finished, so no stream credit
                // is needed. Return connection credit once on this owner loop
                // instead of scheduling a child and locking once per RPC.
                const auto bytes = request.body.size();
                if (bytes > 0x7fff'ffffU - consumed_connection_credit)
                    co_return;
                consumed_connection_credit += static_cast<std::uint32_t>(bytes);
                const bool source = static_cast<bool>(immediate->body_source);
                if (source)
                {
                    if (!start_response_write(stream_id, std::move(*immediate)))
                        co_return;
                }
                else
                    queue_response(stream_id, std::move(*immediate));
                release_request_headers(std::move(request.headers));
                if (source)
                    continue;
                ++batched_responses;
                if (pending_outbound_bytes() >= response_batch_bytes ||
                    (batched_responses % response_batch_time_check_interval == 0 &&
                        std::chrono::steady_clock::now() - batch_started >= response_batch_max_age))
                {
                    if (!(co_await flush_batched_outbound()))
                        co_return;
                    batch_started = std::chrono::steady_clock::now();
                    batched_responses = 0;
                }
                continue;
            }
            (void)background_->run(
                [this, stream_id, request = std::move(request)](
                    cnetmod::cancel_token& token) mutable
                    -> cnetmod::task<std::expected<void, std::error_code>>
                {
                    // Ownership has passed to the application. Reclaim exactly
                    // these bytes, just as a streaming consumer would. Keep the
                    // handler in a structured child: it may genuinely suspend
                    // while another RPC on this connection makes progress.
                    schedule_flow_credit(stream_id, request.body.size());
                    auto response = co_await borrowed_streaming_handler_(request, token);
                    release_request_headers(std::move(request.headers));
                    co_return co_await write_response(stream_id, std::move(response), token);
                });
            continue;
        }
        server_response response;
        if (borrowed_streaming_handler_)
        {
            server_request request{
                .stream_id = stream_id,
                .headers = pending.detached
                    ? std::move(pending.headers)
                    : found->second.take_headers()};
            cancel_token cancellation;
            auto immediate = sync_handler_
                ? sync_handler_(request, cancellation)
                : std::nullopt;
            if (immediate)
                response = std::move(*immediate);
            else
                response = co_await borrowed_streaming_handler_(
                    request, cancellation);
            release_request_headers(std::move(request.headers));
        }
        else if (streaming_handler_)
        {
            server_request request{
                .stream_id = stream_id,
                .headers = found->second.take_headers()};
            cancel_token cancellation;
            response = co_await streaming_handler_(
                std::move(request), cancellation);
        }
        else if (handler_)
        {
            server_request request{
                .stream_id = stream_id,
                .headers = found->second.take_headers(),
                .body = {found->second.body().begin(), found->second.body().end()}};
            response = co_await handler_(std::move(request));
        }
        if (response.body_source)
        {
            if (pending.detached)
            {
                auto [entry, inserted] = streams_.try_emplace(stream_id, stream_id,
                    static_cast<std::int32_t>(peer_.initial_window_size),
                    static_cast<std::int32_t>(local_.initial_window_size), std::vector<header_field>{});
                if (inserted)
                    (void)entry->second.receive_headers({}, true);
            }
            if (!start_response_write(stream_id, std::move(response)))
                co_return;
            batch_started = std::chrono::steady_clock::now();
            batched_responses = 0;
            continue;
        }
        queue_response(stream_id, std::move(response), pending.detached);
        ++batched_responses;
        const bool size_limit_reached =
            pending_outbound_bytes() >= response_batch_bytes;
        const bool time_limit_reached =
            batched_responses % response_batch_time_check_interval == 0 &&
            std::chrono::steady_clock::now() - batch_started >=
                response_batch_max_age;
        if (size_limit_reached || time_limit_reached)
        {
            if (!(co_await flush_batched_outbound()))
                co_return;
            batch_started = std::chrono::steady_clock::now();
            batched_responses = 0;
        }
    }
    if (consumed_connection_credit != 0)
    {
        if (consumed_connection_credit > static_cast<std::uint32_t>(
                                             std::numeric_limits<std::int32_t>::max() - receive_window_))
            co_return;
        receive_window_ += static_cast<std::int32_t>(consumed_connection_credit);
        const std::array<std::byte, 4> update{
            static_cast<std::byte>(consumed_connection_credit >> 24),
            static_cast<std::byte>(consumed_connection_credit >> 16),
            static_cast<std::byte>(consumed_connection_credit >> 8),
            static_cast<std::byte>(consumed_connection_credit)};
        queue_frame({.type = frame_type::window_update}, update);
    }
    ready.clear();
    ready_requests_.swap(ready);
}

auto session::receive(std::span<const std::byte> bytes)
    -> std::expected<void, std::error_code>
{
    // Parse complete transport reads in place. The owned buffer is only needed
    // when a preface or frame crosses a read boundary.
    const bool has_buffered_tail = !input_.empty();
    if (has_buffered_tail)
        input_.insert(input_.end(), bytes.begin(), bytes.end());
    const auto source = has_buffered_tail
        ? std::span<const std::byte>{input_}
        : bytes;
    std::size_t offset = 0;

    if (!received_preface_)
    {
        if (source.size() < client_preface.size())
        {
            if (!has_buffered_tail)
                input_.assign(source.begin(), source.end());
            return {};
        }
        if (std::string_view(reinterpret_cast<const char*>(source.data()),
                client_preface.size()) != client_preface)
            return std::unexpected(std::make_error_code(std::errc::protocol_error));
        offset = client_preface.size();
        received_preface_ = true;
        queue_frame({.type = frame_type::settings, .stream_id = 0},
            encode_settings(local_));
    }
    while (source.size() - offset >= frame_header_size)
    {
        auto header =
            decode_frame_header(source.subspan(offset, frame_header_size));
        if (!header)
            return std::unexpected(header.error());
        if (source.size() - offset < frame_header_size + header->length)
            break;
        auto payload = source.subspan(
            offset + frame_header_size, header->length);
        bool complete_body_follows = false;
        const auto next_offset = offset + frame_header_size + header->length;
        if (buffer_complete_requests_ && header->type == frame_type::headers &&
            (header->flags & (flag_end_headers | flag_end_stream)) == flag_end_headers &&
            source.size() - next_offset >= frame_header_size)
        {
            const auto next = decode_frame_header(
                source.subspan(next_offset, frame_header_size));
            complete_body_follows = next && next->type == frame_type::data &&
                next->stream_id == header->stream_id &&
                (next->flags & (flag_end_stream | flag_padded)) == flag_end_stream &&
                next->length <= local_.max_frame_size &&
                next->length <= body_options_.max_bytes &&
                source.size() - next_offset - frame_header_size >= next->length;
        }
        auto result = process_frame(*header, payload, complete_body_follows);
        if (!result)
        {
            queue_connection_error(
                result.error() == std::make_error_code(std::errc::message_size)
                    ? error_frame_size
                    : error_protocol);
            return result;
        }
        offset += frame_header_size + header->length;
    }

    if (offset == source.size())
        input_.clear();
    else if (has_buffered_tail)
        input_.erase(input_.begin(),
            input_.begin() + static_cast<std::ptrdiff_t>(offset));
    else
        input_.assign(source.begin() + static_cast<std::ptrdiff_t>(offset),
            source.end());
    return {};
}

auto session::process_frame(frame_header header,
    std::span<const std::byte> payload, bool complete_body_follows)
    -> std::expected<void, std::error_code>
{
    if (header.length > local_.max_frame_size)
        return std::unexpected(std::make_error_code(std::errc::message_size));
    if (continuation_stream_id_ != 0 && header.type != frame_type::continuation)
        return std::unexpected(std::make_error_code(std::errc::protocol_error));
    if (header.type == frame_type::settings)
    {
        if (header.stream_id != 0)
            return std::unexpected(std::make_error_code(std::errc::protocol_error));
        if ((header.flags & flag_ack) != 0)
            return payload.empty() ? std::expected<void, std::error_code>{}
                                   : std::unexpected(std::make_error_code(
                                         std::errc::protocol_error));
        const auto previous_initial_window = peer_.initial_window_size;
        if (auto error = decode_settings(payload, peer_); error)
            return std::unexpected(error);
        const auto window_delta = static_cast<std::int64_t>(peer_.initial_window_size) -
            previous_initial_window;
        if (window_delta != 0)
            for (auto& [id, active] : streams_)
                if (!active.send_window().adjust(window_delta))
                    return std::unexpected(std::make_error_code(std::errc::protocol_error));
        // Peer SETTINGS_HEADER_TABLE_SIZE constrains this endpoint's encoder;
        // decoder capacity is controlled by the table-size updates in HPACK blocks.
        encoder_.set_dynamic_table_limit(peer_.header_table_size);
        received_settings_ = true;
        queue_frame(
            {.type = frame_type::settings, .flags = flag_ack, .stream_id = 0}, {});
        drain_pending_responses();
        return {};
    }
    if (header.type == frame_type::headers ||
        header.type == frame_type::continuation)
    {
        if (header.stream_id == 0)
            return std::unexpected(std::make_error_code(std::errc::protocol_error));
        if (header.type == frame_type::continuation &&
            header.stream_id != continuation_stream_id_)
            return std::unexpected(std::make_error_code(std::errc::protocol_error));
        if (header.type == frame_type::headers && continuation_stream_id_ != 0)
            return std::unexpected(std::make_error_code(std::errc::protocol_error));
        std::size_t offset = 0;
        std::size_t padding = 0;
        if (header.type == frame_type::headers && (header.flags & flag_padded))
        {
            if (payload.empty())
                return std::unexpected(std::make_error_code(std::errc::protocol_error));
            padding = std::to_integer<std::uint8_t>(payload[0]);
            offset = 1;
        }
        if (header.type == frame_type::headers && (header.flags & flag_priority))
            offset += 5;
        if (offset > payload.size() || padding > payload.size() - offset)
            return std::unexpected(std::make_error_code(std::errc::protocol_error));
        const auto fragment = payload.subspan(
            offset, payload.size() - offset - padding);
        if ((header.flags & flag_end_headers) == 0)
        {
            header_block_.insert(
                header_block_.end(), fragment.begin(), fragment.end());
            continuation_stream_id_ = header.stream_id;
            continuation_end_stream_ = (header.flags & flag_end_stream) != 0;
            return {};
        }
        continuation_stream_id_ = 0;
        if ((header.stream_id & 1U) == 0 ||
            header.stream_id <= last_peer_stream_id_)
        {
            header_block_.clear();
            continuation_end_stream_ = false;
            return std::unexpected(std::make_error_code(std::errc::protocol_error));
        }
        const bool end_stream =
            continuation_end_stream_ || (header.flags & flag_end_stream) != 0;
        continuation_end_stream_ = false;
        auto reusable_headers = borrowed_streaming_handler_
            ? acquire_request_headers()
            : std::vector<header_field>{};
        // A request completed by its HEADERS frame needs no persistent stream
        // state. Decode it directly into the ready queue and avoid a hash-map
        // node, lookup, and erase on the dominant GET/RPC path.
        if (borrowed_streaming_handler_ && end_stream)
        {
            std::span<const std::byte> encoded_headers = fragment;
            if (header.type == frame_type::continuation)
            {
                header_block_.insert(
                    header_block_.end(), fragment.begin(), fragment.end());
                encoded_headers = header_block_;
            }
            auto decode_result = decoder_.decode(
                encoded_headers, reusable_headers);
            header_block_.clear();
            if (!decode_result)
                return std::unexpected(decode_result.error());
            if (auto error = validate_headers(reusable_headers); error)
                return std::unexpected(error);
            last_peer_stream_id_ = header.stream_id;
            ready_requests_.push_back({header.stream_id,
                std::move(reusable_headers), true});
            return {};
        }
        auto [it, inserted] = streams_.try_emplace(
            header.stream_id, header.stream_id,
            static_cast<std::int32_t>(peer_.initial_window_size),
            static_cast<std::int32_t>(local_.initial_window_size),
            std::move(reusable_headers));
        auto* decoded = inserted ? it->second.prepare_headers(end_stream) : nullptr;
        if (!decoded)
        {
            header_block_.clear();
            return std::unexpected(std::make_error_code(std::errc::protocol_error));
        }
        // Most requests fit in one HEADERS frame. Decode that frame directly
        // from the receive buffer; only CONTINUATION sequences need the owned
        // assembly buffer.
        std::span<const std::byte> encoded_headers = fragment;
        if (header.type == frame_type::continuation)
        {
            header_block_.insert(
                header_block_.end(), fragment.begin(), fragment.end());
            encoded_headers = header_block_;
        }
        auto decode_result = decoder_.decode(encoded_headers, *decoded);
        header_block_.clear();
        if (!decode_result)
        {
            streams_.erase(it);
            return std::unexpected(decode_result.error());
        }
        if (auto error = validate_headers(*decoded); error)
        {
            streams_.erase(it);
            return std::unexpected(error);
        }
        last_peer_stream_id_ = header.stream_id;
        // Synchronous routes may accumulate a bounded small body across reads.
        // Their larger bodies switch to streaming before window exhaustion.
        const bool buffer_request = buffer_complete_requests_ && buffer_filter_ &&
            buffer_filter_(*decoded);
        if (buffer_filter_ && !buffer_request)
            complete_body_follows = false;
        if ((streaming_handler_ || borrowed_streaming_handler_) && !end_stream &&
            !complete_body_follows && !buffer_request)
        {
            start_streaming_request(header.stream_id);
        }
        else if (end_stream)
        {
            ready_requests_.push_back({.stream_id = header.stream_id});
        }
        return {};
    }
    if (header.type == frame_type::data)
    {
        auto it = streams_.find(header.stream_id);
        if (it != streams_.end() && borrowed_streaming_handler_ &&
            !it->second.body_stream())
        {
            const auto limit = std::min<std::size_t>(16U * 1024U, body_options_.max_bytes);
            if (payload.size() > limit - std::min(limit, it->second.body().size()))
                start_streaming_request(header.stream_id);
        }
        if (header.stream_id == 0 ||
            payload.size() >
                static_cast<std::size_t>(std::max(receive_window_, 0)) ||
            it == streams_.end() ||
            !it->second.receive_data(payload,
                (header.flags & flag_end_stream) != 0))
            return std::unexpected(std::make_error_code(std::errc::protocol_error));
        receive_window_ -= static_cast<std::int32_t>(payload.size());
        if (!streaming_handler_ && !borrowed_streaming_handler_ &&
            receive_window_ <= 32'767)
        {
            const auto increment =
                static_cast<std::uint32_t>(65'535 - receive_window_);
            receive_window_ += static_cast<std::int32_t>(increment);
            std::array<std::byte, 4> update{static_cast<std::byte>(increment >> 24),
                static_cast<std::byte>(increment >> 16),
                static_cast<std::byte>(increment >> 8),
                static_cast<std::byte>(increment)};
            queue_frame({.type = frame_type::window_update}, update);
        }
        if ((header.flags & flag_end_stream) != 0 &&
            !it->second.body_stream())
        {
            ready_requests_.push_back({.stream_id = header.stream_id});
        }
        return {};
    }
    if (header.type == frame_type::ping)
    {
        if (header.stream_id != 0 || payload.size() != 8)
            return std::unexpected(std::make_error_code(std::errc::protocol_error));
        if ((header.flags & flag_ack) == 0)
            queue_frame({.type = frame_type::ping, .flags = flag_ack}, payload);
    }
    if (header.type == frame_type::window_update)
    {
        if (payload.size() != 4)
            return std::unexpected(std::make_error_code(std::errc::protocol_error));
        const auto increment = read_u32(payload) & 0x7fff'ffffU;
        if (increment == 0)
            return std::unexpected(std::make_error_code(std::errc::protocol_error));
        if (header.stream_id == 0)
        {
            if (increment > static_cast<std::uint32_t>(
                                std::numeric_limits<std::int32_t>::max() - send_window_))
                return std::unexpected(
                    std::make_error_code(std::errc::protocol_error));
            send_window_ += static_cast<std::int32_t>(increment);
            drain_pending_responses();
            return {};
        }
        // A stream-level update never changes the connection window. Updates
        // racing with local stream retirement are harmless; applying them to
        // the connection window eventually overflowed long-lived multiplexed
        // connections and closed otherwise healthy HTTP/2 sessions.
        if (auto found = streams_.find(header.stream_id);
            found != streams_.end() &&
            !found->second.send_window().increase(increment))
            return std::unexpected(
                std::make_error_code(std::errc::protocol_error));
        drain_pending_responses();
        return {};
    }
    if (header.type == frame_type::rst_stream)
    {
        if (header.stream_id == 0 || payload.size() != 4)
            return std::unexpected(std::make_error_code(std::errc::protocol_error));
        if (auto found = streams_.find(header.stream_id);
            found != streams_.end() && found->second.body_stream())
            found->second.body_stream()->fail(
                std::make_error_code(std::errc::operation_canceled));
        streams_.erase(header.stream_id);
        pending_responses_.erase(header.stream_id);
        std::erase_if(ready_requests_, [&](const ready_request& ready)
            {
                return ready.stream_id == header.stream_id;
            });
        return {};
    }
    if (header.type == frame_type::goaway)
    {
        if (header.stream_id != 0 || payload.size() < 8)
            return std::unexpected(std::make_error_code(std::errc::protocol_error));
        goaway_received_ = true;
        return {};
    }
    return {};
}

auto session::run(std::span<const std::byte> initial) -> cnetmod::task<void>
{
    if (!initial.empty())
    {
        if (auto result = receive(initial); !result)
        {
            (void)co_await flush_outbound();
            co_await shutdown_streaming();
            co_return;
        }
        co_await dispatch_ready();
        if (!(co_await flush_batched_outbound()))
        {
            co_await shutdown_streaming();
            co_return;
        }
    }
    // The transport reader fills the returned range before receive() observes
    // it; do not zero an otherwise opaque receive scratch buffer.
    std::array<std::byte, 16 * 1024> buffer;
    while (true)
    {
        auto read = co_await reader_({buffer.data(), buffer.size()});
        if (!read || *read == 0)
        {
            if (read)
                (void)co_await flush_outbound();
            co_await shutdown_streaming();
            co_return;
        }
        if (auto result = receive(std::span{buffer.data(), *read}); !result)
        {
            (void)co_await flush_outbound();
            co_await shutdown_streaming();
            co_return;
        }
        co_await dispatch_ready();
        if (!(co_await flush_batched_outbound()))
        {
            co_await shutdown_streaming();
            co_return;
        }
    }
}
} // namespace cnetmod::http::v2
