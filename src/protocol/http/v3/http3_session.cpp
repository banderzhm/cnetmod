module;

#include <cnetmod/config.hpp>

#include <cstdio>
#include <cstdlib>

#ifdef CNETMOD_HAS_SSL
    #ifdef CNETMOD_ENABLE_QUIC

module cnetmod.protocol.http.v3.session;

import std;
import cnetmod.core.buffer;
import cnetmod.core.error;
import cnetmod.coro.spawn;
import cnetmod.coro.task;
import cnetmod.coro.cancel;
import cnetmod.protocol.http.semantics;
import cnetmod.protocol.quic;
import cnetmod.protocol.http.v3.frame;
import cnetmod.protocol.http.v3.qpack;
import cnetmod.utils;

namespace cnetmod::http::v3 {

// MSVC does not make a non-exported using-declaration from the primary module
// interface visible to an implementation unit.  Keep these local aliases so
// this implementation has the same unambiguous QUIC spelling on every
// compiler.
using quic::quic_connection;
using quic::stream_id;

namespace {

    struct qpack_wait_cancel_state
    {
        channel<std::monostate>* progress{};
    };

    void cancel_qpack_wait(cnetmod::cancel_token& token) noexcept
    {
        auto* state = static_cast<qpack_wait_cancel_state*>(token.ctx_);
        if (state && state->progress)
            (void)state->progress->try_send({});
    }

    auto wait_for_qpack_progress(channel<std::monostate>& progress,
        cnetmod::cancel_token& token)
        -> task<std::expected<void, std::error_code>>
    {
        if (token.is_cancelled())
            co_return std::unexpected(cnetmod::make_error_code(cnetmod::errc::operation_aborted));
        qpack_wait_cancel_state cancel_state{&progress};
        token.ctx_ = &cancel_state;
        token.cancel_fn_ = &cancel_qpack_wait;
        token.pending_.store(true, std::memory_order_release);
        if (token.is_cancelled())
            cancel_qpack_wait(token);
        const auto notification = co_await progress.receive();
        token.pending_.store(false, std::memory_order_release);
        token.cancel_fn_ = nullptr;
        token.ctx_ = nullptr;
        if (token.is_cancelled())
            co_return std::unexpected(cnetmod::make_error_code(cnetmod::errc::operation_aborted));
        if (!notification)
            co_return std::unexpected(std::make_error_code(std::errc::not_connected));
        co_return {};
    }

    // A peer request stream is discovered while already executing on this
    // connection's I/O context. Posting it again inserts a full reactor turn
    // between STREAM delivery and response generation. Keep this helper local
    // to that proven same-context path; the general spawn() API intentionally
    // retains its thread-safe post semantics.
    struct inline_detached_task
    {
        struct promise_type
        {
            auto get_return_object() noexcept -> inline_detached_task
            {
                return {};
            }

            auto initial_suspend() noexcept -> std::suspend_never
            {
                return {};
            }

            void return_void() noexcept {}

            void unhandled_exception() noexcept
            {
                std::terminate();
            }

            auto final_suspend() noexcept -> std::suspend_never
            {
                return {};
            }
        };
    };

    auto start_peer_stream(task<void> operation) -> inline_detached_task
    {
        co_await std::move(operation);
    }

    template <typename Function> class scope_guard
    {
    public:
        explicit scope_guard(Function function)
            : function_(std::move(function))
        {
        }

        ~scope_guard()
        {
            function_();
        }

        scope_guard(const scope_guard&) = delete;
        auto operator=(const scope_guard&) -> scope_guard& = delete;

    private:
        Function function_;
    };

    constexpr std::uint64_t control_stream_type = 0x00;
    constexpr std::uint64_t qpack_encoder_stream_type = 0x02;
    constexpr std::uint64_t qpack_decoder_stream_type = 0x03;
    constexpr std::size_t stream_read_chunk_size = 2048;

    auto append_varint(std::uint64_t value, byte_buffer& out) -> void
    {
        const auto encoded = quic::encode_varint(value);
        if (!encoded)
            return;
        out.insert(out.end(), encoded->first.begin(),
            encoded->first.begin() + static_cast<std::ptrdiff_t>(encoded->second));
    }

    auto settings_frame_for(const http3_settings& settings) -> byte_buffer
    {
        settings_frame frame;
        if (settings.max_header_list_size != 0U)
            frame.settings.emplace(static_cast<std::uint64_t>(http3_setting_key::max_header_list_size), settings.max_header_list_size);
        if (settings.qpack_max_table_capacity != 0U)
            frame.settings.emplace(static_cast<std::uint64_t>(http3_setting_key::qpack_max_table_capacity), settings.qpack_max_table_capacity);
        if (settings.qpack_blocked_streams != 0U)
            frame.settings.emplace(static_cast<std::uint64_t>(http3_setting_key::qpack_blocked_streams), settings.qpack_blocked_streams);
        return encode_http3_frame(frame);
    }

    auto headers_for(const http3_request& request) -> std::vector<header_field>
    {
        std::vector<header_field> headers;
        headers.push_back({":method", std::string{method_to_string(request.method)}});
        headers.push_back({":scheme", request.scheme});
        headers.push_back({":authority", request.host});
        headers.push_back({":path", request.path.empty() ? "/" : request.path});
        for (const auto& [name, value] : request.headers)
        {
            if (!name.starts_with(':') && !std::ranges::equal(name, "transfer-encoding", {}, [](unsigned char character)
                                              {
                                                  return static_cast<char>(std::tolower(character));
                                              },
                                              [](unsigned char character)
                                              {
                                                  return static_cast<char>(std::tolower(character));
                                              }))
                headers.push_back({name, value});
        }
        return headers;
    }

    auto send_control_stream(quic_connection& connection, const http3_settings& settings)
        -> task<std::expected<stream_id, std::error_code>>
    {
        auto stream = co_await connection.async_open_stream(false);
        if (!stream)
            co_return std::unexpected(stream.error());
        byte_buffer bytes;
        append_varint(control_stream_type, bytes);
        auto settings_bytes = settings_frame_for(settings);
        bytes.insert(bytes.end(), settings_bytes.begin(), settings_bytes.end());
        auto sent = co_await connection.async_send(*stream, bytes, false);
        if (!sent)
            co_return std::unexpected(sent.error());
        co_return *stream;
    }

    auto send_unidirectional_stream_type(quic_connection& connection, std::uint64_t type)
        -> task<std::expected<stream_id, std::error_code>>
    {
        auto stream = co_await connection.async_open_stream(false);
        if (!stream)
            co_return std::unexpected(stream.error());
        byte_buffer preface;
        append_varint(type, preface);
        auto sent = co_await connection.async_send(*stream, preface, false);
        if (!sent)
            co_return std::unexpected(sent.error());
        co_return *stream;
    }

    auto initialize_qpack_streams(quic_connection& connection,
        std::optional<stream_id>& encoder_stream, std::optional<stream_id>& decoder_stream)
        -> task<std::expected<void, std::error_code>>
    {
        if (!encoder_stream)
        {
            auto opened = co_await send_unidirectional_stream_type(connection, qpack_encoder_stream_type);
            if (!opened)
                co_return std::unexpected(opened.error());
            encoder_stream = *opened;
        }
        if (!decoder_stream)
        {
            auto opened = co_await send_unidirectional_stream_type(connection, qpack_decoder_stream_type);
            if (!opened)
                co_return std::unexpected(opened.error());
            decoder_stream = *opened;
        }
        co_return {};
    }

    auto flush_qpack_encoder_instructions(quic_connection& connection, qpack_encoder& encoder,
        const std::optional<stream_id>& stream) -> task<std::expected<void, std::error_code>>
    {
        const auto instructions = encoder.take_encoder_instructions();
        if (instructions.empty())
            co_return {};
        if (!stream)
            co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
        auto sent = co_await connection.async_send(*stream, instructions, false);
        if (!sent)
            co_return std::unexpected(sent.error());
        co_return {};
    }

    auto flush_qpack_decoder_instructions(quic_connection& connection, qpack_decoder& decoder,
        const std::optional<stream_id>& stream) -> task<std::expected<void, std::error_code>>
    {
        const auto instructions = decoder.take_decoder_instructions();
        if (instructions.empty())
            co_return {};
        if (!stream)
            co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
        auto sent = co_await connection.async_send(*stream, instructions, false);
        if (!sent)
            co_return std::unexpected(sent.error());
        co_return {};
    }

    auto response_from_frames(qpack_decoder& decoder, byte_view wire,
        stream_id id, std::deque<std::vector<header_field>>& completed_headers)
        -> std::expected<http3_response, std::error_code>
    {
        http3_response response;
        response.version = http_version::http_3;
        bool headers_seen{};
        bool trailers_seen{};
        bool status_seen{};
        std::size_t offset{};
        while (offset < wire.size())
        {
            auto decoded = decode_http3_frame(wire.subspan(offset));
            if (!decoded)
                return std::unexpected(decoded.error());
            if (decoded->second == 0U)
                return std::unexpected(std::make_error_code(std::errc::protocol_error));
            offset += decoded->second;
            if (const auto* headers = std::get_if<headers_frame>(&decoded->first))
            {
                if (trailers_seen)
                    return std::unexpected(std::make_error_code(std::errc::protocol_error));
                std::expected<std::vector<header_field>, std::error_code> fields;
                if (!completed_headers.empty())
                {
                    fields = std::move(completed_headers.front());
                    completed_headers.pop_front();
                }
                else
                    fields = decoder.decode(headers->encoded_headers, id);
                if (!fields)
                    return std::unexpected(fields.error());
                const auto is_trailer = headers_seen;
                for (const auto& field : *fields)
                {
                    if (!is_trailer && field.name == ":status")
                    {
                        if (status_seen)
                            return std::unexpected(std::make_error_code(std::errc::protocol_error));
                        const auto [end, error] = std::from_chars(field.value.data(), field.value.data() + field.value.size(), response.status);
                        if (error != std::errc{} || end != field.value.data() + field.value.size() || response.status < 100 || response.status > 999)
                            return std::unexpected(std::make_error_code(std::errc::protocol_error));
                        status_seen = true;
                    }
                    else if (!field.name.starts_with(':'))
                        (is_trailer ? response.trailers : response.headers).insert_or_assign(field.name, field.value);
                    else
                        return std::unexpected(std::make_error_code(std::errc::protocol_error));
                }
                if (is_trailer)
                    trailers_seen = true;
                headers_seen = true;
            }
            else if (const auto* data = std::get_if<data_frame>(&decoded->first))
            {
                if (!headers_seen || trailers_seen)
                    return std::unexpected(std::make_error_code(std::errc::protocol_error));
                response.body.append(::utils::conv::to_string_view(data->data));
            }
            else if (const auto* goaway = std::get_if<goaway_frame>(&decoded->first))
            {
                (void)goaway;
                return std::unexpected(std::make_error_code(std::errc::connection_aborted));
            }
        }
        if (!headers_seen || !status_seen)
            return std::unexpected(std::make_error_code(std::errc::protocol_error));
        return response;
    }

    auto request_from_frames(qpack_decoder& decoder, byte_view wire,
        stream_id id)
        -> std::expected<http3_request, std::error_code>
    {
        http3_request request;
        bool headers_seen{};
        bool trailers_seen{};
        bool method_seen{};
        bool scheme_seen{};
        bool path_seen{};
        bool authority_seen{};
        std::size_t offset{};
        while (offset < wire.size())
        {
            auto decoded = decode_http3_frame(wire.subspan(offset));
            if (!decoded)
                return std::unexpected(decoded.error());
            offset += decoded->second;
            if (const auto* headers = std::get_if<headers_frame>(&decoded->first))
            {
                if (trailers_seen)
                    return std::unexpected(std::make_error_code(std::errc::protocol_error));
                auto fields = decoder.decode(headers->encoded_headers, id);
                if (!fields)
                    return std::unexpected(fields.error());
                const auto is_trailer = headers_seen;
                for (const auto& field : *fields)
                {
                    if (!is_trailer && field.name == ":method")
                    {
                        if (method_seen)
                            return std::unexpected(std::make_error_code(std::errc::protocol_error));
                        auto method = string_to_method(field.value);
                        if (!method)
                            return std::unexpected(std::make_error_code(std::errc::protocol_error));
                        request.method = *method;
                        method_seen = true;
                    }
                    else if (field.name == ":path")
                    {
                        if (is_trailer || path_seen)
                            return std::unexpected(std::make_error_code(std::errc::protocol_error));
                        request.path = field.value;
                        path_seen = true;
                    }
                    else if (field.name == ":scheme")
                    {
                        if (is_trailer || scheme_seen)
                            return std::unexpected(std::make_error_code(std::errc::protocol_error));
                        request.scheme = field.value;
                        scheme_seen = true;
                    }
                    else if (field.name == ":authority")
                    {
                        if (is_trailer || authority_seen)
                            return std::unexpected(std::make_error_code(std::errc::protocol_error));
                        request.host = field.value;
                        authority_seen = true;
                    }
                    else if (!field.name.starts_with(':'))
                        (is_trailer ? request.trailers : request.headers).insert_or_assign(field.name, field.value);
                    else
                        return std::unexpected(std::make_error_code(std::errc::protocol_error));
                }
                if (is_trailer)
                    trailers_seen = true;
                headers_seen = true;
            }
            else if (const auto* data = std::get_if<data_frame>(&decoded->first))
            {
                if (!headers_seen || trailers_seen)
                    return std::unexpected(std::make_error_code(std::errc::protocol_error));
                request.body.append(::utils::conv::to_string_view(data->data));
            }
            else if (std::holds_alternative<unknown_frame>(decoded->first))
                continue;
            else
                return std::unexpected(std::make_error_code(std::errc::protocol_error));
        }
        if (!headers_seen || !method_seen || !scheme_seen || !path_seen || !authority_seen || request.host.empty())
            return std::unexpected(std::make_error_code(std::errc::protocol_error));
        return request;
    }

    auto response_frames(qpack_encoder& encoder, const http3_response& response, stream_id id)
        -> std::expected<byte_buffer, std::error_code>
    {
        std::vector<header_field> fields;
        fields.reserve(response.headers.size() + 1U);
        fields.push_back({":status", std::to_string(response.status)});
        for (const auto& [name, value] : response.headers)
            if (!name.starts_with(':') && !std::ranges::equal(name, "transfer-encoding", {}, [](unsigned char character)
                                              {
                                                  return static_cast<char>(std::tolower(character));
                                              },
                                              [](unsigned char character)
                                              {
                                                  return static_cast<char>(std::tolower(character));
                                              }))
                fields.push_back({name, value});
        auto block = encoder.encode(fields, id);
        if (!block)
            return std::unexpected(block.error());
        auto result = encode_http3_frame(headers_frame{*block});
        if (!response.body.empty())
        {
            auto data = encode_http3_frame(
                data_frame{::utils::conv::to_bytes(response.body)});
            result.insert(result.end(), data.begin(), data.end());
        }
        if (!response.trailers.empty())
        {
            std::vector<header_field> trailers;
            trailers.reserve(response.trailers.size());
            for (const auto& [name, value] : response.trailers)
            {
                if (name.starts_with(':'))
                    return std::unexpected(std::make_error_code(std::errc::protocol_error));
                trailers.push_back({name, value});
            }
            auto trailer_block = encoder.encode(trailers, id);
            if (!trailer_block)
                return std::unexpected(trailer_block.error());
            auto trailer_frame = encode_http3_frame(headers_frame{*trailer_block});
            result.insert(result.end(), trailer_frame.begin(), trailer_frame.end());
        }
        return result;
    }

} // namespace

http3_server_session::http3_server_session(quic_connection& connection, server_request_handler handler)
    : conn_(connection), handler_(std::move(handler)), encoder_(0), decoder_(0) {}

http3_server_session::http3_server_session(quic_connection& connection,
    async_server_request_handler handler)
    : conn_(connection), async_handler_(std::move(handler)), encoder_(0), decoder_(0) {}

auto http3_server_session::run() -> task<void>
{
    if (!control_stream_sent_ && !closing_)
    {
        auto sent = co_await send_control_stream(conn_, local_settings_);
        if (sent)
        {
            control_stream_sent_ = true;
            control_stream_ = *sent;
        }
        else
        {
            co_await conn_.async_close(sent.error(), "HTTP/3 control stream setup failed");
            co_return;
        }
        auto qpack_streams = co_await initialize_qpack_streams(conn_, qpack_encoder_stream_, qpack_decoder_stream_);
        if (!qpack_streams)
        {
            co_await conn_.async_close(qpack_streams.error(), "HTTP/3 QPACK stream setup failed");
            co_return;
        }
    }
    while (!closing_ && !conn_.is_closed())
    {
        auto accepted = co_await conn_.async_accept_stream();
        if (!accepted)
            break;
        peer_streams_.add();
        start_peer_stream(service_peer_stream(*accepted));
    }
    co_await peer_streams_.wait();
}

auto http3_server_session::service_peer_stream(stream_id id) -> task<void>
{
    dynamic_buffer wire{stream_read_chunk_size};
    const bool unidirectional = (id & 0x02U) != 0U;
    if (!unidirectional)
        ++active_streams_;
    scope_guard completion{[this, unidirectional]
        {
            if (!unidirectional)
            {
                --active_streams_;
                if (received_goaway_ && active_streams_ == 0U && !closing_)
                {
                    closing_ = true;
                    spawn(conn_.context(),
                        conn_.async_close({}, "peer completed HTTP/3 shutdown"));
                }
            }
            peer_streams_.done();
        }};

    for (;;)
    {
        auto received = co_await conn_.async_recv(
            id, wire.prepare(stream_read_chunk_size));
        if (!received)
        {
            if (!unidirectional && std::getenv("CNETMOD_QUIC_DIAG") != nullptr)
                std::fprintf(stderr,
                    "H3 recv wait sid=%llu wire=%zu error=%d\n",
                    static_cast<unsigned long long>(id), wire.readable_bytes(),
                    received.error().value());
            if (received.error() != std::make_error_code(std::errc::operation_would_block))
                co_return;
            auto ready = co_await conn_.async_wait_readable(id);
            if (!ready)
                co_return;
            continue;
        }
        if (*received == 0U)
        {
            if (!unidirectional && std::getenv("CNETMOD_QUIC_DIAG") != nullptr)
                std::fprintf(stderr, "H3 recv fin sid=%llu wire=%zu\n",
                    static_cast<unsigned long long>(id), wire.readable_bytes());
            break;
        }
        if (!unidirectional && std::getenv("CNETMOD_QUIC_DIAG") != nullptr)
            std::fprintf(stderr, "H3 recv data sid=%llu bytes=%zu\n",
                static_cast<unsigned long long>(id), *received);
        wire.commit(*received);

        if (unidirectional)
        {
            auto validated = process_peer_unidirectional_stream(
                id, wire.readable_view());
            if (validated)
            {
                if (received_goaway_ && active_streams_ == 0U)
                {
                    co_await conn_.async_close({}, "peer completed HTTP/3 shutdown");
                    co_return;
                }
                continue;
            }
            if (validated.error() == std::make_error_code(std::errc::message_size))
                continue;
            co_await conn_.async_close(validated.error(), "invalid HTTP/3 unidirectional stream");
            co_return;
        }
    }

    if (unidirectional)
        co_return;

    auto request = request_from_frames(decoder_, wire.readable_view(), id);
    if (!request)
    {
        co_await conn_.async_close(request.error(), "invalid HTTP/3 request stream");
        co_return;
    }
    auto decoder_flush = co_await flush_qpack_decoder_instructions(conn_, decoder_, qpack_decoder_stream_);
    if (!decoder_flush)
    {
        co_await conn_.async_close(decoder_flush.error(), "HTTP/3 QPACK decoder stream failed");
        co_return;
    }
    http3_response response;
    if (async_handler_)
    {
        cancel_token request_token;
        conn_.register_stream_cancellation(id, request_token);
        scope_guard unregister{[this, id]
            { conn_.unregister_stream_cancellation(id); }};
        auto handled = co_await async_handler_(*request, response, request_token);
        if (!handled)
        {
            if (request_token.is_cancelled())
            {
                (void)conn_.retire_stream(id);
                co_return;
            }
            response.status = status::internal_server_error;
            response.body.clear();
        }
    }
    else if (handler_(*request, response))
    {
        response.status = status::internal_server_error;
        response.body.clear();
    }
    auto encoded = response_frames(encoder_, response, id);
    if (!encoded)
    {
        co_await conn_.async_close(encoded.error(), "HTTP/3 response encoding failed");
        co_return;
    }
    auto encoder_flush = co_await flush_qpack_encoder_instructions(conn_, encoder_, qpack_encoder_stream_);
    if (!encoder_flush)
    {
        co_await conn_.async_close(encoder_flush.error(), "HTTP/3 QPACK encoder stream failed");
        co_return;
    }
    const auto response_sent = co_await conn_.async_send(id, *encoded, true);
    if (!response_sent)
    {
        if (std::getenv("CNETMOD_QUIC_DIAG") != nullptr)
            std::fprintf(stderr,
                "H3 response send failed sid=%llu error=%d\n",
                static_cast<unsigned long long>(id),
                response_sent.error().value());
    }
    else
    {
        const auto retired = conn_.retire_stream(id);
        if (!retired && std::getenv("CNETMOD_QUIC_DIAG") != nullptr)
            std::fprintf(stderr,
                "H3 retire failed sid=%llu error=%d\n",
                static_cast<unsigned long long>(id), retired.error().value());
    }
}

auto read_varint(byte_view input, std::size_t& used)
    -> std::expected<std::uint64_t, std::error_code>
{
    if (input.empty())
        return std::unexpected(std::make_error_code(std::errc::message_size));
    const auto length = static_cast<std::size_t>(1U << (std::to_integer<std::uint8_t>(input.front()) >> 6U));
    if (input.size() < length)
        return std::unexpected(std::make_error_code(std::errc::message_size));
    std::uint64_t value = std::to_integer<std::uint8_t>(input.front()) & 0x3fU;
    for (std::size_t index = 1; index < length; ++index)
        value = (value << 8U) | std::to_integer<std::uint8_t>(input[index]);
    used = length;
    return value;
}

auto validate_peer_uni_stream_payload(qpack_decoder& decoder, qpack_encoder& encoder,
    bool& control_seen, bool& settings_seen, bool& encoder_stream_seen,
    bool& decoder_stream_seen, bool& received_goaway, std::uint64_t& goaway_stream_id,
    const bool peer_is_server, std::uint64_t type, byte_view payload,
    bool continuation, std::size_t& consumed)
    -> std::expected<void, std::error_code>
{
    consumed = 0U;
    switch (type)
    {
    case control_stream_type:
    {
        // The stream type can arrive before the first complete control frame.
        // Do not latch the stream as seen until its mandatory SETTINGS frame
        // has been decoded, otherwise a normal segmented QUIC delivery is
        // indistinguishable from a duplicate control stream.
        if ((!continuation && control_seen) ||
            (continuation && (!control_seen || !settings_seen)))
            return std::unexpected(std::make_error_code(std::errc::protocol_error));
        std::size_t offset{};
        while (offset < payload.size())
        {
            auto frame = decode_http3_frame(payload.subspan(offset));
            if (!frame)
            {
                if (frame.error() ==
                    std::make_error_code(std::errc::message_size))
                    break;
                return std::unexpected(frame.error());
            }
            offset += frame->second;
            if (const auto* settings = std::get_if<settings_frame>(&frame->first))
            {
                if (settings_seen)
                    return std::unexpected(std::make_error_code(std::errc::protocol_error));
                for (const auto& [identifier, setting_value] : settings->settings)
                {
                    if (identifier >= 0x02U && identifier <= 0x05U)
                        return std::unexpected(
                            std::make_error_code(std::errc::protocol_error));
                    if (!std::holds_alternative<std::uint64_t>(setting_value))
                        return std::unexpected(
                            std::make_error_code(std::errc::protocol_error));
                    const auto value = std::get<std::uint64_t>(setting_value);
                    if ((identifier == static_cast<std::uint64_t>(http3_setting_key::enable_connect_protocol) ||
                            identifier == static_cast<std::uint64_t>(http3_setting_key::h3_datagram)) &&
                        value > 1U)
                        return std::unexpected(
                            std::make_error_code(std::errc::protocol_error));
                }
                // A peer-advertised capacity authorizes, but never requires,
                // dynamic-table use.  Keep the encoder in static/literal mode
                // until its encoder stream has a continuously consumed peer
                // decoder.  This preserves the QPACK ordering invariant: a
                // header block must not reference an insertion the peer has
                // not processed yet.
                const auto capacity = settings->settings.find(
                    static_cast<std::uint64_t>(
                        http3_setting_key::qpack_max_table_capacity));
                if (capacity != settings->settings.end())
                    encoder.set_max_table_capacity(
                        std::get<std::uint64_t>(capacity->second));
                auto blocked = settings->settings.find(static_cast<std::uint64_t>(http3_setting_key::qpack_blocked_streams));
                if (blocked != settings->settings.end() && std::holds_alternative<std::uint64_t>(blocked->second))
                    encoder.set_max_blocked_streams(std::get<std::uint64_t>(blocked->second));
                settings_seen = true;
                control_seen = true;
            }
            else if (const auto* goaway = std::get_if<goaway_frame>(&frame->first))
            {
                // A server GOAWAY identifies a client-initiated bidirectional
                // stream (RFC 9114 §5.2); clients use a Push ID instead.
                if (!settings_seen || goaway->stream_id > goaway_stream_id ||
                    (peer_is_server && (goaway->stream_id & 0x03U) != 0U))
                    return std::unexpected(std::make_error_code(std::errc::protocol_error));
                received_goaway = true;
                goaway_stream_id = goaway->stream_id;
            }
            else if (std::holds_alternative<max_push_id_frame>(frame->first) && !peer_is_server)
            {
                // Clients may advertise the largest Push ID they are willing
                // to accept. A server must consume this control frame even
                // when it does not implement server push.
            }
            // SETTINGS is mandatory and must be the first control frame.
            else if (!settings_seen || std::holds_alternative<data_frame>(frame->first) ||
                std::holds_alternative<headers_frame>(frame->first) ||
                std::holds_alternative<push_promise_frame>(frame->first) ||
                std::holds_alternative<cancel_push_frame>(frame->first) ||
                std::holds_alternative<max_push_id_frame>(frame->first))
            {
                return std::unexpected(std::make_error_code(std::errc::protocol_error));
            }
        }
        consumed = offset;
        if (!continuation && !settings_seen)
            return std::unexpected(std::make_error_code(std::errc::message_size));
        return {};
    }
    case qpack_encoder_stream_type:
        if ((!continuation && encoder_stream_seen) ||
            (continuation && !encoder_stream_seen))
            return std::unexpected(std::make_error_code(std::errc::protocol_error));
        if (!continuation)
            encoder_stream_seen = true;
        if (payload.empty())
            return {};
        consumed = payload.size();
        return decoder.process_encoder_instructions(payload);
    case qpack_decoder_stream_type:
        if ((!continuation && decoder_stream_seen) ||
            (continuation && !decoder_stream_seen))
            return std::unexpected(std::make_error_code(std::errc::protocol_error));
        if (!continuation)
            decoder_stream_seen = true;
        if (payload.empty())
            return {};
        consumed = payload.size();
        return encoder.process_decoder_instructions(payload);
    default:
        // Unknown unidirectional stream types are explicitly ignored by RFC 9114.
        consumed = payload.size();
        return {};
    }
}

auto validate_peer_uni_stream(qpack_decoder& decoder, qpack_encoder& encoder,
    bool& control_seen, bool& settings_seen, bool& encoder_stream_seen,
    bool& decoder_stream_seen, bool& received_goaway, std::uint64_t& goaway_stream_id,
    const bool peer_is_server, stream_id id, byte_view bytes,
    cnetmod::flat_map<stream_id, std::uint64_t>& stream_types,
    cnetmod::flat_map<stream_id, std::size_t>& processed_bytes)
    -> std::expected<void, std::error_code>
{
    if ((id & 0x02U) == 0U ||
        (((id & 0x01U) != 0U) != peer_is_server))
        return std::unexpected(std::make_error_code(std::errc::protocol_error));

    const auto processed = processed_bytes.find(id);
    if (processed != processed_bytes.end())
    {
        if (bytes.size() < processed->second)
            return std::unexpected(std::make_error_code(std::errc::protocol_error));
        if (bytes.size() == processed->second)
            return {};
        const auto type = stream_types.find(id);
        if (type == stream_types.end())
            return std::unexpected(std::make_error_code(std::errc::protocol_error));
        std::size_t consumed{};
        auto result = validate_peer_uni_stream_payload(decoder, encoder,
            control_seen, settings_seen, encoder_stream_seen, decoder_stream_seen,
            received_goaway, goaway_stream_id, peer_is_server, type->second,
            bytes.subspan(processed->second), true, consumed);
        if (result)
            processed->second += consumed;
        return result;
    }

    std::size_t type_size{};
    auto type = read_varint(bytes, type_size);
    if (!type)
        return std::unexpected(type.error());
    std::size_t consumed{};
    auto result = validate_peer_uni_stream_payload(decoder, encoder,
        control_seen, settings_seen, encoder_stream_seen, decoder_stream_seen,
        received_goaway, goaway_stream_id, peer_is_server, *type,
        bytes.subspan(type_size), false, consumed);
    if (result)
    {
        stream_types.emplace(id, *type);
        processed_bytes.emplace(id, type_size + consumed);
    }
    return result;
}

auto http3_server_session::close() -> task<void>
{
    if (!closing_)
    {
        closing_ = true;
        co_await conn_.async_close({}, "HTTP/3 server session closed");
    }
}

auto http3_server_session::send_goaway(stream_id last_stream) -> task<void>
{
    if (!control_stream_sent_)
        co_await run();
    if (!control_stream_)
        co_return;
    // Server GOAWAY carries a client-initiated bidirectional stream ID.
    if ((last_stream & 0x03U) != 0U)
    {
        co_await conn_.async_close(std::make_error_code(std::errc::protocol_error), "invalid HTTP/3 GOAWAY stream ID");
        co_return;
    }
    auto frame = encode_http3_frame(goaway_frame{last_stream, {}, {}});
    (void)co_await conn_.async_send(*control_stream_, frame, false);
}

auto http3_server_session::get_active_streams_count() const noexcept -> std::size_t
{
    return active_streams_;
}

http3_client_session::http3_client_session(quic_connection& connection, client_request_handler handler)
    : conn_(connection), handler_(std::move(handler)), encoder_(0), decoder_(0) {}

auto http3_client_session::configure_local_settings(http3_settings settings) noexcept -> void
{
    settings_ = settings;
    // Local SETTINGS authorize the peer encoder's dynamic-table capacity.
    // Our encoder remains at zero until peer SETTINGS authorize its budget.
    decoder_.set_max_table_capacity(settings.qpack_max_table_capacity);
    decoder_.set_max_blocked_streams(settings.qpack_blocked_streams);
}

auto http3_client_session::connect() -> task<std::expected<void, std::error_code>>
{
    if (control_stream_sent_)
        co_return {};
    auto sent = co_await send_control_stream(conn_, settings_);
    if (!sent)
        co_return std::unexpected(sent.error());
    control_stream_sent_ = true;
    control_stream_ = *sent;
    auto qpack_streams = co_await initialize_qpack_streams(conn_, qpack_encoder_stream_, qpack_decoder_stream_);
    if (!qpack_streams)
        co_return std::unexpected(qpack_streams.error());
    co_return {};
}

auto http3_client_session::close() -> task<void>
{
    co_await conn_.async_close({}, "HTTP/3 client session closed");
}

auto http3_client_session::close_all() -> task<void>
{
    co_await close();
}

auto http3_client_session::send_request(const http3_request& request)
    -> task<std::expected<http3_response, std::error_code>>
{
    // Preserve the established hot path exactly: a normal HTTP/3 request
    // neither creates a cancellation token nor registers a wake-up callback.
    if (received_goaway_)
        co_return std::unexpected(std::make_error_code(std::errc::connection_aborted));
    auto connected = co_await connect();
    if (!connected)
        co_return std::unexpected(connected.error());

    auto stream = co_await conn_.async_open_stream(true);
    if (!stream)
        co_return std::unexpected(stream.error());
    auto block = encoder_.encode(headers_for(request), *stream);
    if (!block)
        co_return std::unexpected(block.error());
    auto encoder_flush = co_await flush_qpack_encoder_instructions(conn_, encoder_, qpack_encoder_stream_);
    if (!encoder_flush)
        co_return std::unexpected(encoder_flush.error());
    auto headers = encode_http3_frame(headers_frame{*block});
    auto sent = co_await conn_.async_send(*stream, headers, request.body.empty() && request.trailers.empty());
    if (!sent)
        co_return std::unexpected(sent.error());
    if (!request.body.empty())
    {
        auto data = encode_http3_frame(data_frame{::utils::conv::to_bytes(request.body)});
        sent = co_await conn_.async_send(*stream, data, request.trailers.empty());
        if (!sent)
            co_return std::unexpected(sent.error());
    }
    if (!request.trailers.empty())
    {
        std::vector<header_field> trailers;
        trailers.reserve(request.trailers.size());
        for (const auto& [name, value] : request.trailers)
        {
            if (name.starts_with(':'))
                co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
            trailers.push_back({name, value});
        }
        auto trailer_block = encoder_.encode(trailers, *stream);
        if (!trailer_block)
            co_return std::unexpected(trailer_block.error());
        encoder_flush = co_await flush_qpack_encoder_instructions(conn_, encoder_, qpack_encoder_stream_);
        if (!encoder_flush)
            co_return std::unexpected(encoder_flush.error());
        auto trailer_frame = encode_http3_frame(headers_frame{*trailer_block});
        sent = co_await conn_.async_send(*stream, trailer_frame, true);
        if (!sent)
            co_return std::unexpected(sent.error());
    }

    dynamic_buffer wire{stream_read_chunk_size};
    for (;;)
    {
        auto received = co_await conn_.async_recv(*stream, wire.prepare(stream_read_chunk_size));
        if (!received)
        {
            if (received.error() == std::make_error_code(std::errc::operation_would_block))
            {
                auto ready = co_await conn_.async_wait_readable(*stream);
                if (ready)
                    continue;
                co_return std::unexpected(ready.error());
            }
            co_return std::unexpected(received.error());
        }
        if (*received == 0U)
            break;
        wire.commit(*received);
    }
    auto& completed_headers = completed_headers_[*stream];
    auto response = response_from_frames(decoder_, wire.readable_view(), *stream,
        completed_headers);
    while (!response && response.error() == std::make_error_code(std::errc::resource_unavailable_try_again))
    {
        const auto progress = co_await qpack_progress_.receive();
        if (!progress)
            co_return std::unexpected(std::make_error_code(std::errc::not_connected));
        response = response_from_frames(decoder_, wire.readable_view(), *stream,
            completed_headers);
    }
    completed_headers_.erase(*stream);
    if (!response)
        co_return std::unexpected(response.error());
    auto decoder_flush = co_await flush_qpack_decoder_instructions(conn_, decoder_, qpack_decoder_stream_);
    if (!decoder_flush)
        co_return std::unexpected(decoder_flush.error());
    (void)conn_.retire_stream(*stream);
    co_return *response;
}

auto http3_client_session::send_request(const http3_request& request,
    cnetmod::cancel_token& token)
    -> task<std::expected<http3_response, std::error_code>>
{
    if (token.is_cancelled())
        co_return std::unexpected(cnetmod::make_error_code(cnetmod::errc::operation_aborted));
    if (received_goaway_)
        co_return std::unexpected(std::make_error_code(std::errc::connection_aborted));
    auto connected = co_await connect();
    if (!connected)
        co_return std::unexpected(connected.error());

    auto stream = co_await conn_.async_open_stream(true);
    if (!stream)
        co_return std::unexpected(stream.error());
    auto cancel_stream = [&]() -> task<void> {
        (void)co_await conn_.async_cancel_stream(*stream);
    };
    auto block = encoder_.encode(headers_for(request), *stream);
    if (!block)
        co_return std::unexpected(block.error());
    auto encoder_flush = co_await flush_qpack_encoder_instructions(conn_, encoder_, qpack_encoder_stream_);
    if (!encoder_flush)
        co_return std::unexpected(encoder_flush.error());
    if (token.is_cancelled())
    {
        co_await cancel_stream();
        co_return std::unexpected(cnetmod::make_error_code(cnetmod::errc::operation_aborted));
    }
    auto headers = encode_http3_frame(headers_frame{*block});
    auto sent = co_await conn_.async_send(*stream, headers, request.body.empty() && request.trailers.empty());
    if (!sent)
        co_return std::unexpected(sent.error());
    if (token.is_cancelled())
    {
        co_await cancel_stream();
        co_return std::unexpected(cnetmod::make_error_code(cnetmod::errc::operation_aborted));
    }
    if (!request.body.empty())
    {
        auto data = encode_http3_frame(
            data_frame{::utils::conv::to_bytes(request.body)});
        sent = co_await conn_.async_send(*stream, data, request.trailers.empty());
        if (!sent)
            co_return std::unexpected(sent.error());
        if (token.is_cancelled())
        {
            co_await cancel_stream();
            co_return std::unexpected(cnetmod::make_error_code(cnetmod::errc::operation_aborted));
        }
    }
    if (!request.trailers.empty())
    {
        std::vector<header_field> trailers;
        trailers.reserve(request.trailers.size());
        for (const auto& [name, value] : request.trailers)
        {
            if (name.starts_with(':'))
                co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
            trailers.push_back({name, value});
        }
        auto trailer_block = encoder_.encode(trailers, *stream);
        if (!trailer_block)
            co_return std::unexpected(trailer_block.error());
        encoder_flush = co_await flush_qpack_encoder_instructions(conn_, encoder_, qpack_encoder_stream_);
        if (!encoder_flush)
            co_return std::unexpected(encoder_flush.error());
        auto trailer_frame = encode_http3_frame(headers_frame{*trailer_block});
        sent = co_await conn_.async_send(*stream, trailer_frame, true);
        if (!sent)
            co_return std::unexpected(sent.error());
        if (token.is_cancelled())
        {
            co_await cancel_stream();
            co_return std::unexpected(cnetmod::make_error_code(cnetmod::errc::operation_aborted));
        }
    }

    dynamic_buffer wire{stream_read_chunk_size};
    for (;;)
    {
        auto received = co_await conn_.async_recv(
            *stream, wire.prepare(stream_read_chunk_size));
        if (!received)
        {
            if (received.error() == std::make_error_code(std::errc::operation_would_block))
            {
                auto ready = co_await conn_.async_wait_readable(*stream, token);
                if (ready)
                    continue;
                if (token.is_cancelled())
                    co_await cancel_stream();
                co_return std::unexpected(ready.error());
            }
            co_return std::unexpected(received.error());
        }
        if (*received == 0U)
            break;
        wire.commit(*received);
    }
    auto& completed_headers = completed_headers_[*stream];
    auto response = response_from_frames(decoder_, wire.readable_view(), *stream,
        completed_headers);
    while (!response && response.error() == std::make_error_code(std::errc::resource_unavailable_try_again))
    {
        const auto progress = co_await wait_for_qpack_progress(qpack_progress_, token);
        if (!progress)
        {
            if (token.is_cancelled())
                co_await cancel_stream();
            co_return std::unexpected(progress.error());
        }
        response = response_from_frames(decoder_, wire.readable_view(), *stream,
            completed_headers);
    }
    completed_headers_.erase(*stream);
    if (!response)
        co_return std::unexpected(response.error());
    auto decoder_flush = co_await flush_qpack_decoder_instructions(conn_, decoder_, qpack_decoder_stream_);
    if (!decoder_flush)
        co_return std::unexpected(decoder_flush.error());
    (void)conn_.retire_stream(*stream);
    co_return *response;
}

auto http3_server_session::process_peer_unidirectional_stream(stream_id id,
    byte_view bytes) -> std::expected<void, std::error_code>
{
    return validate_peer_uni_stream(decoder_, encoder_, peer_control_stream_seen_, peer_settings_seen_,
        peer_qpack_encoder_stream_seen_, peer_qpack_decoder_stream_seen_, received_goaway_,
        goaway_stream_id_, false, id, bytes, peer_unidirectional_stream_types_,
        peer_unidirectional_stream_bytes_);
}

auto http3_client_session::process_peer_unidirectional_stream(stream_id id,
    byte_view bytes) -> std::expected<void, std::error_code>
{
    auto result = validate_peer_uni_stream(decoder_, encoder_, peer_control_stream_seen_, peer_settings_seen_,
        peer_qpack_encoder_stream_seen_, peer_qpack_decoder_stream_seen_, received_goaway_,
        goaway_stream_id_, true, id, bytes, peer_unidirectional_stream_types_,
        peer_unidirectional_stream_bytes_);
    if (!result)
        return result;
    auto completed = decoder_.take_completed_header_blocks();
    for (auto& block : completed)
        completed_headers_[block.stream_id].push_back(std::move(block.headers));
    if (!completed.empty())
        (void)qpack_progress_.try_send({});
    return {};
}

auto http3_client_session::accepting_requests() const noexcept -> bool
{
    return !received_goaway_ && !conn_.is_closed();
}

auto make_http3_server_session(quic_connection& connection, server_request_handler handler)
    -> std::unique_ptr<http3_server_session>
{
    return std::make_unique<http3_server_session>(connection, std::move(handler));
}

auto make_http3_server_session(quic_connection& connection,
    async_server_request_handler handler) -> std::unique_ptr<http3_server_session>
{
    return std::make_unique<http3_server_session>(connection, std::move(handler));
}

auto make_http3_client_session(quic_connection& connection, client_request_handler handler)
    -> std::unique_ptr<http3_client_session>
{
    return std::make_unique<http3_client_session>(connection, std::move(handler));
}

} // namespace cnetmod::http::v3

    #endif
#endif
