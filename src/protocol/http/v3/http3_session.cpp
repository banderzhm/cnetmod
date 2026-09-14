module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.http.v3.session;

import std;
import cnetmod.core.buffer;
import cnetmod.core.error;
import cnetmod.core.log;
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

    [[nodiscard]] constexpr auto response_forbids_content(int status_code,
        bool head_request) noexcept -> bool
    {
        return head_request || (status_code >= 100 && status_code < 200) ||
            status_code == 204 || status_code == 304;
    }

    auto response_declared_length(const http3_response& response)
        -> std::expected<std::optional<std::uint64_t>, std::error_code>
    {
        const auto field = response.headers.find("content-length");
        if (field == response.headers.end())
            return std::nullopt;
        const auto text = std::string_view{field->second};
        if (text.empty())
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        std::uint64_t length{};
        const auto [end, error] = std::from_chars(
            text.data(), text.data() + text.size(), length);
        if (error != std::errc{} || end != text.data() + text.size())
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        return length;
    }

    // RFC 9297 uses a Quarter Stream ID as the HTTP Datagram context ID for
    // request/response streams. A WebTransport session is identified by its
    // CONNECT stream, so its context ID is the QUIC stream ID divided by four.
    [[nodiscard]] constexpr auto webtransport_datagram_context_id(stream_id session_id) noexcept
        -> std::uint64_t
    {
        return static_cast<std::uint64_t>(session_id) / 4U;
    }

    [[nodiscard]] constexpr auto webtransport_session_id_from_datagram_context(
        std::uint64_t context_id) noexcept -> std::optional<stream_id>
    {
        if (context_id > std::numeric_limits<stream_id>::max() / 4U)
            return std::nullopt;
        return static_cast<stream_id>(context_id * 4U);
    }

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
    constexpr std::uint64_t push_stream_type = 0x01;
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
        if (settings.enable_datagram)
            frame.settings.emplace(static_cast<std::uint64_t>(http3_setting_key::h3_datagram), 1U);
        // Chrome WebTransport deployments still gate the feature on their
        // former Datagram SETTINGS identifier.  Keep the RFC 9297 setting
        // above authoritative; peers that do not know this identifier ignore
        // it as required by HTTP/3.
        if (settings.enable_webtransport)
            frame.settings.emplace(static_cast<std::uint64_t>(http3_setting_key::h3_datagram_chrome_legacy), 1U);
        if (settings.enable_connect_protocol)
            frame.settings.emplace(static_cast<std::uint64_t>(http3_setting_key::enable_connect_protocol), 1U);
        if (settings.enable_webtransport)
            frame.settings.emplace(static_cast<std::uint64_t>(http3_setting_key::enable_webtransport), 1U);
        if (settings.webtransport_max_sessions != 0U)
            frame.settings.emplace(static_cast<std::uint64_t>(http3_setting_key::webtransport_max_sessions),
                settings.webtransport_max_sessions);
        return encode_http3_frame(frame);
    }

    auto headers_for(const http3_request& request) -> std::vector<header_field>
    {
        std::vector<header_field> headers;
        // Four pseudo-fields are emitted for every ordinary request, plus an
        // optional extended-CONNECT protocol field. Reserve them alongside
        // user headers so client-side request construction stays allocation
        // bounded on the common HTTP/3 path.
        headers.reserve(request.headers.size() + 4U +
            (request.protocol.empty() ? 0U : 1U));
        headers.push_back({":method", std::string{method_to_string(request.method)}});
        if (!request.protocol.empty())
            headers.push_back({":protocol", request.protocol});
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

    auto send_priority_update(quic_connection& connection,
        std::optional<stream_id> control_stream, cnetmod::async_mutex& control_mutex,
        cnetmod::flat_map<stream_id, http_priority>& published_priorities,
        stream_id request_stream, http_priority priority)
        -> task<std::expected<void, std::error_code>>
    {
        // RFC 9218 distinguishes a request-stream element ID from a Push ID.
        // This API schedules request streams only; Push IDs intentionally use
        // the response's explicit http3_push::priority when that is added.
        if ((request_stream & 0x03U) != 0U || priority.urgency > 7U)
            co_return std::unexpected(std::make_error_code(std::errc::invalid_argument));

        if (!connection.set_stream_priority(request_stream, priority.urgency,
                priority.incremental))
            co_return std::unexpected(
                std::make_error_code(std::errc::resource_unavailable_try_again));
        co_await control_mutex.lock();
        cnetmod::async_lock_guard guard{control_mutex, std::adopt_lock};
        if (!control_stream)
            co_return std::unexpected(std::make_error_code(std::errc::not_connected));
        if (const auto current = published_priorities.find(request_stream);
            current != published_priorities.end() &&
            current->second.urgency == priority.urgency &&
            current->second.incremental == priority.incremental)
            co_return {};
        const auto frame = encode_http3_frame(priority_update_frame{
            request_stream, format_http_priority(priority)});
        const auto sent = co_await connection.async_send(*control_stream,
            frame, false);
        if (!sent)
            co_return std::unexpected(sent.error());
        published_priorities.insert_or_assign(request_stream, priority);
        co_return {};
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

    auto request_from_frames(qpack_decoder& decoder, byte_view wire,
        stream_id id, std::deque<std::vector<header_field>>& completed_headers)
        -> std::expected<http3_request, std::error_code>;

    auto response_from_frames(qpack_decoder& decoder, byte_view wire,
        stream_id id, std::deque<std::vector<header_field>>& completed_headers,
        cnetmod::flat_map<std::uint64_t, http3_request>* promised_pushes = nullptr,
        const std::optional<std::uint64_t>* max_push_id = nullptr,
        channel<std::monostate>* push_promise_progress = nullptr)
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
                for (auto& field : *fields)
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
                        (is_trailer ? response.trailers : response.headers).insert_or_assign(std::move(field.name), std::move(field.value));
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
            else if (const auto* promise = std::get_if<push_promise_frame>(&decoded->first))
            {
                // PUSH_PROMISE is legal only on a client-initiated request
                // stream and only after the client explicitly grants a limit.
                if (!promised_pushes || !max_push_id ||
                    promise->promised_stream_id > **max_push_id ||
                    promised_pushes->contains(promise->promised_stream_id))
                    return std::unexpected(std::make_error_code(std::errc::protocol_error));
                auto fields = decoder.decode(promise->encoded_headers, id);
                if (!fields)
                    return std::unexpected(fields.error());
                completed_headers.push_back(std::move(*fields));
                const auto placeholder = encode_http3_frame(headers_frame{{}});
                auto request = request_from_frames(decoder,
                    byte_view{placeholder.data(), placeholder.size()}, id,
                    completed_headers);
                if (!request || !request->body.empty() || !request->trailers.empty())
                    return std::unexpected(request ? std::make_error_code(std::errc::protocol_error) : request.error());
                promised_pushes->emplace(promise->promised_stream_id,
                    std::move(*request));
                if (push_promise_progress)
                    (void)push_promise_progress->try_send({});
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
        stream_id id, std::deque<std::vector<header_field>>& completed_headers)
        -> std::expected<http3_request, std::error_code>
    {
        http3_request request;
        request.request_stream = id;
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
                std::expected<std::vector<header_field>, std::error_code> fields;
                if (!completed_headers.empty())
                {
                    fields = std::move(completed_headers.front());
                    completed_headers.pop_front();
                }
                else
                    fields = decoder.decode(headers->encoded_headers, id);
                if (!fields)
                {

                    return std::unexpected(fields.error());
                }
                const auto is_trailer = headers_seen;
                // `header_map` is a contiguous flat map. Reserve once from
                // the already-decoded QPACK field count so normal request
                // headers do not repeatedly move key/value strings while
                // pseudo fields and application fields are classified.
                auto& target_headers = is_trailer ? request.trailers : request.headers;
                target_headers.reserve(target_headers.size() + fields->size());
                for (auto& field : *fields)
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
                        request.path = std::move(field.value);
                        path_seen = true;
                    }
                    else if (field.name == ":scheme")
                    {
                        if (is_trailer || scheme_seen)
                            return std::unexpected(std::make_error_code(std::errc::protocol_error));
                        request.scheme = std::move(field.value);
                        scheme_seen = true;
                    }
                    else if (field.name == ":authority")
                    {
                        if (is_trailer || authority_seen)
                            return std::unexpected(std::make_error_code(std::errc::protocol_error));
                        request.host = std::move(field.value);
                        authority_seen = true;
                    }
                    else if (field.name == ":protocol")
                    {
                        // HTTP/3 does not require a particular ordering among
                        // request pseudo-header fields.  In particular,
                        // wtransport may encode :protocol before :method.
                        // Collect it first and validate the CONNECT relation
                        // after the complete header block has been decoded.
                        if (is_trailer || !request.protocol.empty() || field.value.empty())
                            return std::unexpected(std::make_error_code(std::errc::protocol_error));
                        request.protocol = std::move(field.value);
                    }
                    else if (!field.name.starts_with(':'))
                        target_headers.insert_or_assign(std::move(field.name), std::move(field.value));
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
        if (!request.protocol.empty() && request.method != http_method::CONNECT)
            return std::unexpected(std::make_error_code(std::errc::protocol_error));
        return request;
    }

    auto response_frames(qpack_encoder& encoder, const http3_response& response, stream_id id,
        bool suppress_body = false)
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
        if (!suppress_body && !response.body.empty())
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

    auto response_headers_frame(qpack_encoder& encoder, const http3_response& response,
        stream_id id) -> std::expected<byte_buffer, std::error_code>
    {
        std::vector<header_field> fields;
        fields.reserve(response.headers.size() + 2U);
        fields.push_back({":status", std::to_string(response.status)});
        for (const auto& [name, value] : response.headers)
        {
            if (name.starts_with(':') || std::ranges::equal(name, "transfer-encoding", {}, [](unsigned char character)
                                             {
                                                 return static_cast<char>(std::tolower(character));
                                             },
                                             [](unsigned char character)
                                             {
                                                 return static_cast<char>(std::tolower(character));
                                             }))
                continue;
            fields.push_back({name, value});
        }
        if (response.body_source && response.body_source->content_length() &&
            response.headers.find("content-length") == response.headers.end())
            fields.push_back({"content-length",
                std::to_string(*response.body_source->content_length())});
        auto block = encoder.encode(fields, id);
        if (!block)
            return std::unexpected(block.error());
        return encode_http3_frame(headers_frame{*block});
    }

    auto response_trailers_frame(qpack_encoder& encoder, const http3_response& response,
        stream_id id) -> std::expected<byte_buffer, std::error_code>
    {
        if (response.trailers.empty())
            return byte_buffer{};
        std::vector<header_field> trailers;
        trailers.reserve(response.trailers.size());
        for (const auto& [name, value] : response.trailers)
        {
            if (name.starts_with(':'))
                return std::unexpected(std::make_error_code(std::errc::protocol_error));
            trailers.push_back({name, value});
        }
        auto block = encoder.encode(trailers, id);
        if (!block)
            return std::unexpected(block.error());
        return encode_http3_frame(headers_frame{*block});
    }

} // namespace

auto make_webtransport_connect_request(std::string host, std::string path)
    -> http3_request
{
    http3_request request;
    request.method = http_method::CONNECT;
    request.host = std::move(host);
    request.path = path.empty() ? "/" : std::move(path);
    request.protocol = "webtransport";
    request.headers.insert_or_assign("sec-webtransport-http3-draft02", "1");
    return request;
}

namespace {
    auto is_webtransport_session_id(stream_id id) noexcept -> bool
    {
        // An HTTP/3 WebTransport session is always initiated by the client on
        // a bidirectional stream (RFC WebTransport over HTTP/3, section 3).
        return (id & 0x03U) == 0U;
    }
} // namespace

struct webtransport_session_state
{
    webtransport_session_state(quic_connection& connection, stream_id id) noexcept
        : connection(&connection), session_id(id), streams(64), datagrams(256), closes(1) {}

    quic_connection* connection{};
    stream_id session_id{};
    std::atomic<bool> closed{};
    channel<stream_id> streams;
    channel<std::vector<std::byte>> datagrams;
    channel<webtransport_close_info> closes;
    // All mutation is dispatched through the owning QUIC connection's
    // serialized execution domain.  This is actor-local state, not a
    // cross-thread concurrent container.
    std::unordered_set<stream_id> owned_streams;
};

namespace {
    constexpr std::uint64_t close_webtransport_session_capsule_type = 0x2843U;
    // A CLOSE_WEBTRANSPORT_SESSION capsule is control-plane metadata.  Keep
    // incomplete frame/capsule buffering bounded when a peer never finishes
    // the CONNECT stream.
    constexpr std::size_t max_webtransport_close_buffer_size = 64U * 1024U;

    [[nodiscard]] auto is_valid_utf8(std::string_view text) noexcept -> bool
    {
        for (std::size_t index{}; index < text.size();)
        {
            const auto first = static_cast<unsigned char>(text[index++]);
            if (first <= 0x7FU)
                continue;
            std::size_t continuation_count{};
            std::uint32_t codepoint{};
            if (first >= 0xC2U && first <= 0xDFU)
            {
                continuation_count = 1U;
                codepoint = first & 0x1FU;
            }
            else if (first >= 0xE0U && first <= 0xEFU)
            {
                continuation_count = 2U;
                codepoint = first & 0x0FU;
            }
            else if (first >= 0xF0U && first <= 0xF4U)
            {
                continuation_count = 3U;
                codepoint = first & 0x07U;
            }
            else
                return false;
            if (text.size() - index < continuation_count)
                return false;
            for (std::size_t offset{}; offset < continuation_count; ++offset)
            {
                const auto continuation = static_cast<unsigned char>(text[index++]);
                if ((continuation & 0xC0U) != 0x80U)
                    return false;
                codepoint = (codepoint << 6U) | (continuation & 0x3FU);
            }
            if ((continuation_count == 2U && codepoint < 0x800U) ||
                (continuation_count == 3U && codepoint < 0x10000U) ||
                (codepoint >= 0xD800U && codepoint <= 0xDFFFU) || codepoint > 0x10FFFFU)
                return false;
        }
        return true;
    }

    auto consume_webtransport_close_capsules(quic_connection& connection,
        std::shared_ptr<webtransport_session_state> state) -> task<void>
    {
        std::vector<std::byte> wire;
        std::vector<std::byte> capsules;
        std::array<std::byte, 16384> read_buffer{};
        while (!connection.is_closed() && !state->closed.load())
        {
            const auto received = co_await connection.async_recv(state->session_id,
                mutable_buffer{read_buffer.data(), read_buffer.size()});
            if (!received)
            {
                if (received.error() == std::make_error_code(std::errc::operation_would_block))
                {
                    const auto ready = co_await connection.async_wait_readable(state->session_id);
                    if (ready)
                        continue;
                }
                break;
            }
            if (*received == 0U)
                break;
            if (*received > max_webtransport_close_buffer_size - wire.size())
            {
                state->closed.store(true);
                state->streams.close();
                state->datagrams.close();
                state->closes.close();
                co_return;
            }
            wire.insert(wire.end(), read_buffer.begin(), read_buffer.begin() + *received);
            std::size_t frame_offset{};
            while (frame_offset < wire.size())
            {
                const auto frame = decode_http3_frame(byte_view{wire.data() + frame_offset,
                    wire.size() - frame_offset});
                if (!frame)
                {
                    if (frame.error() == std::make_error_code(std::errc::message_size))
                        break;
                    state->closes.close();
                    co_return;
                }
                frame_offset += frame->second;
                if (const auto* data = std::get_if<data_frame>(&frame->first))
                {
                    if (data->data.size() > max_webtransport_close_buffer_size - capsules.size())
                    {
                        state->closed.store(true);
                        state->streams.close();
                        state->datagrams.close();
                        state->closes.close();
                        co_return;
                    }
                    capsules.insert(capsules.end(), data->data.begin(), data->data.end());
                }
            }
            if (frame_offset != 0U)
                wire.erase(wire.begin(), wire.begin() + static_cast<std::ptrdiff_t>(frame_offset));

            std::size_t capsule_offset{};
            while (capsule_offset < capsules.size())
            {
                const auto type = quic::decode_varint(byte_view{capsules.data() + capsule_offset,
                    capsules.size() - capsule_offset});
                if (!type)
                    break;
                const auto length = quic::decode_varint(byte_view{capsules.data() + capsule_offset + type->second,
                    capsules.size() - capsule_offset - type->second});
                if (!length || capsules.size() - capsule_offset < type->second + length->second + length->first)
                    break;
                const auto payload_offset = capsule_offset + type->second + length->second;
                if (type->first == close_webtransport_session_capsule_type)
                {
                    const auto code = quic::decode_varint(byte_view{capsules.data() + payload_offset,
                        static_cast<std::size_t>(length->first)});
                    if (!code || code->second > length->first)
                    {
                        state->closes.close();
                        co_return;
                    }
                    const auto reason_start = payload_offset + code->second;
                    const auto reason_size = static_cast<std::size_t>(length->first) - code->second;
                    const std::string_view reason{reinterpret_cast<const char*>(capsules.data() + reason_start),
                        reason_size};
                    if (!is_valid_utf8(reason))
                    {
                        state->closed.store(true);
                        state->streams.close();
                        state->datagrams.close();
                        state->closes.close();
                        co_return;
                    }
                    webtransport_close_info close{code->first,
                        std::string(reason)};
                    (void)state->closes.try_send(std::move(close));
                    state->closed.store(true);
                    state->streams.close();
                    state->datagrams.close();
                    co_return;
                }
                capsule_offset = payload_offset + static_cast<std::size_t>(length->first);
            }
            if (capsule_offset != 0U)
                capsules.erase(capsules.begin(), capsules.begin() + static_cast<std::ptrdiff_t>(capsule_offset));
        }
        state->closes.close();
    }
} // namespace

webtransport_session::webtransport_session(quic_connection& connection,
    stream_id session_id) noexcept
    : state_(std::make_shared<webtransport_session_state>(connection, session_id))
{
}

auto encode_webtransport_bidirectional_stream_preface(stream_id session_id)
    -> std::expected<std::vector<std::byte>, std::error_code>
{
    if (!is_webtransport_session_id(session_id))
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    const auto encoded = quic::encode_varint(session_id);
    if (!encoded)
        return std::unexpected(encoded.error());
    return std::vector<std::byte>(encoded->first.begin(),
        encoded->first.begin() + encoded->second);
}

auto encode_webtransport_unidirectional_stream_preface(stream_id session_id)
    -> std::expected<std::vector<std::byte>, std::error_code>
{
    if (!is_webtransport_session_id(session_id))
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    const auto type = quic::encode_varint(webtransport_unidirectional_stream_type);
    const auto session = quic::encode_varint(session_id);
    if (!type)
        return std::unexpected(type.error());
    if (!session)
        return std::unexpected(session.error());
    std::vector<std::byte> result;
    result.reserve(type->second + session->second);
    result.insert(result.end(), type->first.begin(), type->first.begin() + type->second);
    result.insert(result.end(), session->first.begin(), session->first.begin() + session->second);
    return result;
}

auto decode_webtransport_stream_preface(byte_view bytes, bool unidirectional)
    -> std::expected<std::pair<stream_id, std::size_t>, std::error_code>
{
    std::size_t used{};
    if (unidirectional)
    {
        const auto type = quic::decode_varint(bytes);
        if (!type)
            return std::unexpected(type.error());
        if (type->first != webtransport_unidirectional_stream_type)
            return std::unexpected(std::make_error_code(std::errc::protocol_error));
        used = type->second;
    }
    else
    {
        // aioquic currently emits the older 0x41 marker before the session
        // ID. The RFC 9220 form begins directly with a client bidi stream ID
        // (a multiple of four), so the two forms are unambiguous.
        const auto type = quic::decode_varint(bytes);
        if (!type)
            return std::unexpected(type.error());
        if (type->first == webtransport_legacy_bidirectional_stream_type)
            used = type->second;
    }
    const auto session = quic::decode_varint(bytes.subspan(used));
    if (!session)
        return std::unexpected(session.error());
    if (!is_webtransport_session_id(session->first))
        return std::unexpected(std::make_error_code(std::errc::protocol_error));
    return std::pair{static_cast<stream_id>(session->first), used + session->second};
}

auto webtransport_session::id() const noexcept -> stream_id
{
    return state_ ? state_->session_id : stream_id{};
}

auto webtransport_session::is_open() const noexcept -> bool
{
    return state_ && state_->connection != nullptr && !state_->closed.load(std::memory_order_acquire) &&
        !state_->connection->is_closed();
}

auto webtransport_session::send_datagram(byte_view payload)
    -> task<std::expected<void, std::error_code>>
{
    if (!is_open())
        co_return std::unexpected(std::make_error_code(std::errc::not_connected));
    const auto encoded = encode_http_datagram(
        {webtransport_datagram_context_id(state_->session_id), payload});
    co_return co_await state_->connection->async_send_datagram(encoded);
}

auto webtransport_session::open_bidirectional_stream()
    -> task<std::expected<stream_id, std::error_code>>
{
    if (!is_open())
        co_return std::unexpected(std::make_error_code(std::errc::not_connected));
    const auto stream = co_await state_->connection->async_open_stream(true);
    if (!stream)
        co_return std::unexpected(stream.error());
    const auto preface = encode_webtransport_bidirectional_stream_preface(state_->session_id);
    if (!preface)
        co_return std::unexpected(preface.error());
    const auto sent = co_await state_->connection->async_send(*stream, *preface, false);
    if (!sent)
    {
        (void)co_await state_->connection->async_cancel_stream(*stream);
        co_return std::unexpected(sent.error());
    }
    state_->owned_streams.insert(*stream);
    co_return *stream;
}

auto webtransport_session::open_unidirectional_stream()
    -> task<std::expected<stream_id, std::error_code>>
{
    if (!is_open())
        co_return std::unexpected(std::make_error_code(std::errc::not_connected));
    const auto stream = co_await state_->connection->async_open_stream(false);
    if (!stream)
        co_return std::unexpected(stream.error());
    const auto preface = encode_webtransport_unidirectional_stream_preface(state_->session_id);
    if (!preface)
        co_return std::unexpected(preface.error());
    const auto sent = co_await state_->connection->async_send(*stream, *preface, false);
    if (!sent)
    {
        (void)co_await state_->connection->async_cancel_stream(*stream);
        co_return std::unexpected(sent.error());
    }
    state_->owned_streams.insert(*stream);
    co_return *stream;
}

auto webtransport_session::send_stream(stream_id stream, byte_view payload, bool finish)
    -> task<std::expected<void, std::error_code>>
{
    if (!is_open())
        co_return std::unexpected(std::make_error_code(std::errc::not_connected));
    if (!state_->owned_streams.contains(stream))
        co_return std::unexpected(std::make_error_code(std::errc::operation_not_permitted));
    co_return co_await state_->connection->async_send(stream, payload, finish);
}

auto webtransport_session::receive_stream(stream_id stream, mutable_buffer output)
    -> task<std::expected<std::size_t, std::error_code>>
{
    if (!is_open())
        co_return std::unexpected(std::make_error_code(std::errc::not_connected));
    if (!state_->owned_streams.contains(stream))
        co_return std::unexpected(std::make_error_code(std::errc::operation_not_permitted));
    // QUIC delivers a child stream's preface and application bytes in
    // independent packets.  Do not expose that packet boundary as a failed
    // WebTransport read: wait until the stream becomes readable, then retry.
    // `async_wait_readable` checks readiness before sleeping, so this has no
    // lost-wakeup window when a packet arrives between the two operations.
    for (;;)
    {
        const auto received = co_await state_->connection->async_recv(stream, output);
        if (received || received.error() != std::make_error_code(std::errc::operation_would_block))
        {

            co_return received;
        }
        const auto ready = co_await state_->connection->async_wait_readable(stream);
        if (!ready)
            co_return std::unexpected(ready.error());
    }
}

namespace {
    auto make_webtransport_close_capsule(std::uint64_t application_error_code,
        std::string_view reason) -> std::expected<std::vector<std::byte>, std::error_code>
    {
        if (reason.size() > 1024U)
            return std::unexpected(std::make_error_code(std::errc::message_size));
        if (!is_valid_utf8(reason))
            return std::unexpected(std::make_error_code(std::errc::invalid_argument));
        const auto type = quic::encode_varint(close_webtransport_session_capsule_type);
        const auto code = quic::encode_varint(application_error_code);
        if (!type)
            return std::unexpected(type.error());
        if (!code)
            return std::unexpected(code.error());
        const auto length = quic::encode_varint(code->second + reason.size());
        if (!length)
            return std::unexpected(length.error());
        std::vector<std::byte> capsule;
        capsule.reserve(type->second + length->second + code->second + reason.size());
        capsule.insert(capsule.end(), type->first.begin(), type->first.begin() + type->second);
        capsule.insert(capsule.end(), length->first.begin(), length->first.begin() + length->second);
        capsule.insert(capsule.end(), code->first.begin(), code->first.begin() + code->second);
        for (const auto character : reason)
            capsule.push_back(static_cast<std::byte>(static_cast<unsigned char>(character)));
        return capsule;
    }
} // namespace

auto webtransport_session::close(std::uint64_t application_error_code,
    std::string_view reason) -> task<std::expected<void, std::error_code>>
{
    if (!state_ || state_->closed.exchange(true, std::memory_order_acq_rel))
        co_return {};
    if (state_->connection == nullptr || state_->connection->is_closed())
        co_return std::unexpected(std::make_error_code(std::errc::not_connected));
    state_->streams.close();
    state_->datagrams.close();
    state_->closes.close();
    if (application_error_code == 0U && reason.empty())
    {
        const auto sent = co_await state_->connection->async_send(
            state_->session_id, std::span<const std::byte>{}, true);
        if (!sent)
            co_return std::unexpected(sent.error());
        co_return {};
    }
    const auto capsule = make_webtransport_close_capsule(application_error_code, reason);
    if (!capsule)
        co_return std::unexpected(capsule.error());
    const auto frame = encode_http3_frame(data_frame{
        byte_view{capsule->data(), capsule->size()}});
    const auto sent = co_await state_->connection->async_send(state_->session_id, frame, true);
    if (!sent)
        co_return std::unexpected(sent.error());
    co_return {};
}

auto webtransport_session::accept_stream()
    -> task<std::expected<stream_id, std::error_code>>
{
    if (!state_)
        co_return std::unexpected(std::make_error_code(std::errc::not_connected));
    auto stream = co_await state_->streams.receive();
    if (!stream)
        co_return std::unexpected(std::make_error_code(std::errc::not_connected));
    co_return *stream;
}

auto webtransport_session::receive_datagram()
    -> task<std::expected<std::vector<std::byte>, std::error_code>>
{
    if (!state_)
        co_return std::unexpected(std::make_error_code(std::errc::not_connected));
    auto payload = co_await state_->datagrams.receive();
    if (!payload)
        co_return std::unexpected(std::make_error_code(std::errc::not_connected));
    co_return std::move(*payload);
}

auto webtransport_session::wait_for_close()
    -> task<std::expected<webtransport_close_info, std::error_code>>
{
    if (!state_)
        co_return std::unexpected(std::make_error_code(std::errc::not_connected));
    auto close = co_await state_->closes.receive();
    if (!close)
        co_return std::unexpected(std::make_error_code(std::errc::not_connected));
    co_return std::move(*close);
}

http3_server_session::http3_server_session(quic_connection& connection, server_request_handler handler)
    : conn_(connection), handler_(std::move(handler)), encoder_(0), decoder_(0)
{
    local_settings_.enable_datagram = conn_.datagrams_configured();
    local_settings_.enable_connect_protocol = local_settings_.enable_datagram;
    local_settings_.enable_webtransport = false;
    local_settings_.webtransport_max_sessions = 0U;
}

http3_server_session::http3_server_session(quic_connection& connection,
    async_server_request_handler handler)
    : conn_(connection), async_handler_(std::move(handler)), encoder_(0), decoder_(0)
{
    local_settings_.enable_datagram = conn_.datagrams_configured();
    local_settings_.enable_connect_protocol = local_settings_.enable_datagram;
    local_settings_.enable_webtransport = false;
    local_settings_.webtransport_max_sessions = 0U;
}

http3_server_session::http3_server_session(quic_connection& connection,
    streaming_server_request_handler handler)
    : conn_(connection), streaming_handler_(std::move(handler)), encoder_(0), decoder_(0)
{
    local_settings_.enable_datagram = conn_.datagrams_configured();
    local_settings_.enable_connect_protocol = local_settings_.enable_datagram;
    local_settings_.enable_webtransport = false;
    local_settings_.webtransport_max_sessions = 0U;
}

http3_server_session::http3_server_session(quic_connection& connection,
    async_webtransport_handler handler)
    : conn_(connection), webtransport_handler_(std::move(handler)), encoder_(0), decoder_(0)
{
    local_settings_.enable_datagram = conn_.datagrams_configured();
    local_settings_.enable_connect_protocol = local_settings_.enable_datagram;
    local_settings_.enable_webtransport = local_settings_.enable_datagram;
    local_settings_.webtransport_max_sessions = local_settings_.enable_webtransport ? 1U : 0U;
}

http3_server_session::http3_server_session(quic_connection& connection,
    http3_server_handlers handlers)
    : conn_(connection), async_handler_(std::move(handlers.request)), webtransport_handler_(std::move(handlers.webtransport)), encoder_(0), decoder_(0)
{
    local_settings_.enable_datagram = conn_.datagrams_configured();
    local_settings_.enable_connect_protocol = local_settings_.enable_datagram;
    local_settings_.enable_webtransport = local_settings_.enable_datagram &&
        static_cast<bool>(webtransport_handler_);
    local_settings_.webtransport_max_sessions = local_settings_.enable_webtransport ? 1U : 0U;
}

auto http3_server_session::configure_local_settings(http3_settings settings) noexcept -> void
{
    // Datagram and WebTransport settings are derived from the QUIC transport
    // configuration and handler kind; a listener-level QPACK setting must not
    // accidentally advertise an application feature the connection cannot
    // carry.  QPACK is the only part intentionally overridden here.
    local_settings_.max_header_list_size = settings.max_header_list_size;
    local_settings_.qpack_max_table_capacity = settings.qpack_max_table_capacity;
    local_settings_.qpack_blocked_streams = settings.qpack_blocked_streams;
    decoder_.set_max_table_capacity(settings.qpack_max_table_capacity);
    decoder_.set_max_blocked_streams(settings.qpack_blocked_streams);
}

auto http3_server_session::configure_push_cancellation_observer(
    server_push_cancellation_observer observer) -> void
{
    push_cancellation_observer_ = std::move(observer);
}

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
        // Keep each peer-stream coroutine owned by the I/O context instead of
        // an unowned eager detached frame.  A session can accept another
        // connection's stream while an earlier stream is suspended; ownership
        // through spawn() prevents that re-entrant interleaving from dropping
        // the later coroutine before its first receive awaiter is installed.
        // peer_streams_ keeps this session alive until every spawned child
        // invokes its scope guard.
        spawn(conn_.context(), service_peer_stream(*accepted));
    }
    co_await peer_streams_.wait();
}

auto http3_server_session::service_peer_stream(stream_id id) -> task<void>
{

    dynamic_buffer wire{stream_read_chunk_size};
    // Explicit streaming handlers can start once the request HEADERS frame is
    // complete. Keep the remaining DATA/trailer frames in `wire` for the
    // concurrent body pump below; synchronous and legacy async handlers retain
    // the complete-body path.
    std::optional<http3_request> streaming_request;
    const bool unidirectional = (id & 0x02U) != 0U;
    // A child stream can be accepted before the coroutine servicing its
    // CONNECT stream has registered the WebTransport session.  Therefore the
    // decision cannot depend on the *current* session map: with a
    // WebTransport handler every bidirectional stream must first have its
    // preface classified (and may need to wait for more preface bytes).
    // Otherwise a fragmented 0x41 + session-id legacy preface is permanently
    // mistaken for an HTTP/3 request frame by this coroutine.  This applies
    // to unidirectional children as well: their WebTransport stream type is
    // distinct from HTTP/3's control/QPACK stream types, so non-matches fall
    // through to the normal HTTP/3 validation below.
    bool stream_classified = !webtransport_handler_;
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
    // Filled by the WebTransport classifier when the stream is instead an
    // ordinary HTTP request. It must outlive the initial receive loop so the
    // normal request path can reuse the single QPACK decode below.
    std::optional<http3_request> pre_parsed_request;

    for (;;)
    {

        auto received = co_await conn_.async_recv(
            id, wire.prepare(stream_classified ? stream_read_chunk_size : 1U));
        if (!received)
        {

            if (received.error() != std::make_error_code(std::errc::operation_would_block))
                co_return;
            auto ready = co_await conn_.async_wait_readable(id);
            if (!ready)
                co_return;
            continue;
        }
        if (*received == 0U)
        {

            break;
        }

        wire.commit(*received);

        if (!stream_classified)
        {
            const auto routed = route_webtransport_stream(
                id, wire.readable_view(), unidirectional);
            if (!routed)
            {
                if (routed.error() == std::make_error_code(std::errc::message_size))
                    continue;
                if (routed.error() ==
                    std::make_error_code(std::errc::resource_unavailable_try_again))
                {
                    // The peer opened a valid WebTransport child stream before
                    // the CONNECT handler published its session.  Wait for
                    // that one-shot publication without consuming or
                    // reclassifying the already buffered preface.
                    pending_webtransport_streams_.fetch_add(1U,
                        std::memory_order_acq_rel);
                    const auto retry = route_webtransport_stream(
                        id, wire.readable_view(), unidirectional);
                    if (retry && *retry)
                    {
                        pending_webtransport_streams_.fetch_sub(1U,
                            std::memory_order_acq_rel);
                        co_return;
                    }
                    if (!retry && retry.error() != std::make_error_code(std::errc::resource_unavailable_try_again))
                    {
                        pending_webtransport_streams_.fetch_sub(1U,
                            std::memory_order_acq_rel);
                        co_await conn_.async_close(retry.error(),
                            "invalid WebTransport stream preface");
                        co_return;
                    }
                    (void)co_await webtransport_registration_.receive();
                    pending_webtransport_streams_.fetch_sub(1U,
                        std::memory_order_acq_rel);
                    continue;
                }
                co_await conn_.async_close(routed.error(),
                    "invalid WebTransport stream preface");
                co_return;
            }
            if (*routed)
                co_return;
            stream_classified = true;
        }

        if (unidirectional)
        {
            auto validated = co_await process_peer_unidirectional_stream(
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

        if (streaming_handler_ && !streaming_request)
        {
            // A request stream must begin with one complete HEADERS frame.
            // DATA may already have arrived in the same UDP delivery, so only
            // consume the header frame here and leave the rest for the body
            // pump. QPACK-blocked headers are retried as decoder instructions
            // arrive on the peer encoder stream.
            auto first = decode_http3_frame(wire.readable_view());
            if (first && std::holds_alternative<headers_frame>(first->first))
            {
                std::expected<http3_request, std::error_code> parsed;
                for (;;)
                {
                    {
                        co_await request_header_mutex_.lock();
                        cnetmod::async_lock_guard header_guard{
                            request_header_mutex_, std::adopt_lock};
                        parsed = request_from_frames(
                            decoder_, wire.readable_view().first(first->second), id,
                            completed_headers_[id]);
                        if (parsed)
                        {
                            auto decoder_flush = co_await flush_qpack_decoder_instructions(
                                conn_, decoder_, qpack_decoder_stream_);
                            if (!decoder_flush)
                                parsed = std::unexpected(decoder_flush.error());
                        }
                    }
                    if (parsed)
                        break;
                    if (parsed.error() != std::make_error_code(std::errc::resource_unavailable_try_again))
                    {
                        co_await conn_.async_close(parsed.error(),
                            "invalid HTTP/3 request headers");
                        co_return;
                    }
                    // The block is retained by QPACK.  Do not read the body
                    // stream while it is blocked: a later DATA frame cannot
                    // make HEADERS decodable, and retrying it as one combined
                    // buffer turns a valid request into a protocol error.
                    const auto progress = co_await qpack_progress_.receive();
                    if (!progress)
                    {
                        co_await conn_.async_close(
                            std::make_error_code(std::errc::not_connected),
                            "HTTP/3 QPACK encoder stream closed");
                        co_return;
                    }
                }
                parsed->body_stream = std::make_shared<request_body_stream>();
                streaming_request = std::move(*parsed);
                wire.consume(first->second);
                break;
            }
        }

        // Extended CONNECT stays writable for the lifetime of a WebTransport
        // session. Its request is therefore complete at HEADERS, rather than at
        // FIN as it is for ordinary HTTP requests. A listener that supports
        // WebTransport must inspect a request before it knows whether it is an
        // Extended CONNECT. Keep a successful ordinary-request parse and reuse it
        // below: parsing the same HEADERS twice mutates QPACK decoder state twice
        // (including duplicate Header Acknowledgements), which can stall an
        // otherwise ordinary response.
        if (webtransport_handler_)
        {
            std::expected<http3_request, std::error_code> parsed_request;
            {
                co_await request_header_mutex_.lock();
                cnetmod::async_lock_guard header_guard{
                    request_header_mutex_, std::adopt_lock};
                parsed_request = request_from_frames(decoder_, wire.readable_view(), id,
                    completed_headers_[id]);
                if (parsed_request)
                {
                    auto decoder_flush = co_await flush_qpack_decoder_instructions(
                        conn_, decoder_, qpack_decoder_stream_);
                    if (!decoder_flush)
                        parsed_request = std::unexpected(decoder_flush.error());
                }
            }

            if (parsed_request && parsed_request->method == http_method::CONNECT &&
                parsed_request->protocol == "webtransport" && parsed_request->body.empty() &&
                parsed_request->trailers.empty())
            {
                if (!local_settings_.enable_datagram)
                {
                    (void)co_await conn_.async_cancel_stream(id, 0U);
                    co_return;
                }

                http3_response accepted;
                // Chromium still performs the WebTransport HTTP/3 draft
                // negotiation.  A successful Extended CONNECT must echo the
                // request's version marker so the browser can distinguish a
                // WebTransport-capable 200 response from a generic CONNECT
                // tunnel response.
                if (const auto version = parsed_request->headers.find(
                        "sec-webtransport-http3-draft02");
                    version != parsed_request->headers.end())
                    accepted.headers.emplace(version->first, version->second);
                // Publish the session before exposing the successful CONNECT
                // response.  A peer is allowed to create a child stream as
                // soon as it receives 2xx; registering afterwards races the
                // child-stream classifier and can consume application bytes
                // while it still looks for a session owner.
                webtransport_session session{conn_, id};
                webtransport_sessions_.insert_or_assign(id, session.state());
                const auto pending = pending_webtransport_streams_.load(
                    std::memory_order_acquire);
                for (std::size_t index = 0; index < pending; ++index)
                    (void)webtransport_registration_.try_send({});
                {
                    co_await response_header_mutex_.lock();
                    cnetmod::async_lock_guard header_guard{
                        response_header_mutex_, std::adopt_lock};
                    auto encoded = response_frames(encoder_, accepted, id);
                    if (!encoded)
                    {
                        webtransport_sessions_.erase(id);
                        co_await conn_.async_close(encoded.error(),
                            "HTTP/3 WebTransport response encoding failed");
                        co_return;
                    }
                    auto encoder_flush = co_await flush_qpack_encoder_instructions(
                        conn_, encoder_, qpack_encoder_stream_);
                    if (!encoder_flush)
                    {
                        webtransport_sessions_.erase(id);
                        co_await conn_.async_close(encoder_flush.error(),
                            "HTTP/3 QPACK encoder stream failed");
                        co_return;
                    }
                    if (!(co_await conn_.async_send(id, *encoded, false)))
                    {
                        webtransport_sessions_.erase(id);
                        co_return;
                    }
                }

                spawn(conn_.context(), consume_webtransport_close_capsules(conn_, session.state()));
                if (!datagram_dispatcher_started_)
                {
                    datagram_dispatcher_started_ = true;
                    spawn(conn_.context(), dispatch_datagrams());
                }
                cancel_token request_token;
                conn_.register_stream_cancellation(id, request_token);
                scope_guard unregister{[this, id]
                    {
                        conn_.unregister_stream_cancellation(id);
                    }};
                auto handled = co_await webtransport_handler_(*parsed_request, session, request_token);
                if (!handled && !request_token.is_cancelled())
                    (void)co_await conn_.async_cancel_stream(id, 0U);
                else if (session.is_open())
                    (void)co_await session.close();
                webtransport_sessions_.erase(id);
                (void)conn_.retire_stream(id);
                co_return;
            }
            if (parsed_request)
                pre_parsed_request = std::move(*parsed_request);
        }
    }

    if (unidirectional)
        co_return;

    std::optional<http3_request> request;
    if (streaming_request)
    {
        request = std::move(streaming_request);
    }
    else if (pre_parsed_request)
    {
        request = std::move(pre_parsed_request);
    }
    else
    {
        for (;;)
        {
            std::expected<http3_request, std::error_code> parsed_request;
            {
                co_await request_header_mutex_.lock();
                cnetmod::async_lock_guard header_guard{
                    request_header_mutex_, std::adopt_lock};
                parsed_request = request_from_frames(decoder_, wire.readable_view(), id,
                    completed_headers_[id]);
                if (parsed_request)
                {
                    auto decoder_flush = co_await flush_qpack_decoder_instructions(
                        conn_, decoder_, qpack_decoder_stream_);
                    if (!decoder_flush)
                        parsed_request = std::unexpected(decoder_flush.error());
                }
            }
            if (parsed_request)
            {
                request = std::move(*parsed_request);
                break;
            }
            if (parsed_request.error() != std::make_error_code(std::errc::resource_unavailable_try_again))
            {
                co_await conn_.async_close(parsed_request.error(),
                    "invalid HTTP/3 request stream");
                co_return;
            }
            const auto progress = co_await qpack_progress_.receive();
            if (!progress)
            {
                co_await conn_.async_close(
                    std::make_error_code(std::errc::not_connected),
                    "HTTP/3 QPACK encoder stream closed");
                co_return;
            }
        }
    }

    http3_response response;
    if (request->body_stream)
    {
        // The handler and the body pump run concurrently. A bounded channel
        // supplies real back-pressure: once the handler stops consuming, the
        // QUIC receive loop stops reading and stream flow control takes over.
        auto body_stream = request->body_stream;
        cancel_token request_token;
        conn_.register_stream_cancellation(id, request_token);
        scope_guard unregister{[this, id]
            {
                conn_.unregister_stream_cancellation(id);
            }};
        scope_guard close_body{[body_stream]
            {
                body_stream->close();
            }};

        auto consume_body = [&]() -> task<std::expected<void, std::error_code>>
        {
            // Wake a handler waiting for the terminal nullopt once the peer
            // FIN (or any body-pump error/cancellation) ends this coroutine.
            scope_guard body_done{[body_stream]
                {
                    body_stream->close();
                }};
            bool trailers_seen = false;
            auto process_available = [&]()
                -> task<std::expected<void, std::error_code>>
            {
                while (wire.readable_bytes() != 0U)
                {
                    auto decoded = decode_http3_frame(wire.readable_view());
                    if (!decoded)
                    {
                        if (decoded.error() ==
                            std::make_error_code(std::errc::message_size))
                            co_return {};
                        co_return std::unexpected(decoded.error());
                    }
                    if (const auto* data = std::get_if<data_frame>(&decoded->first))
                    {
                        if (trailers_seen)
                            co_return std::unexpected(
                                std::make_error_code(std::errc::protocol_error));
                        request_body_chunk chunk{data->data.begin(), data->data.end()};
                        wire.consume(decoded->second);
                        if (!(co_await body_stream->send(std::move(chunk))))
                            co_return std::unexpected(
                                std::make_error_code(std::errc::operation_canceled));
                    }
                    else if (const auto* headers =
                                 std::get_if<headers_frame>(&decoded->first))
                    {
                        if (trailers_seen)
                            co_return std::unexpected(
                                std::make_error_code(std::errc::protocol_error));
                        std::expected<std::vector<header_field>, std::error_code> fields;
                        {
                            co_await request_header_mutex_.lock();
                            cnetmod::async_lock_guard header_guard{
                                request_header_mutex_, std::adopt_lock};
                            auto& completed_headers = completed_headers_[id];
                            if (!completed_headers.empty())
                            {
                                fields = std::move(completed_headers.front());
                                completed_headers.pop_front();
                            }
                            else
                                fields = decoder_.decode(headers->encoded_headers, id);
                            if (fields)
                            {
                                auto decoder_flush = co_await flush_qpack_decoder_instructions(
                                    conn_, decoder_, qpack_decoder_stream_);
                                if (!decoder_flush)
                                    fields = std::unexpected(decoder_flush.error());
                            }
                        }
                        if (!fields)
                            co_return std::unexpected(fields.error());
                        wire.consume(decoded->second);
                        for (const auto& field : *fields)
                        {
                            if (field.name.starts_with(':'))
                                co_return std::unexpected(
                                    std::make_error_code(std::errc::protocol_error));
                            request->trailers.insert_or_assign(field.name, field.value);
                        }
                        trailers_seen = true;
                    }
                    else
                    {
                        co_return std::unexpected(
                            std::make_error_code(std::errc::protocol_error));
                    }
                }
                co_return {};
            };

            for (;;)
            {
                if (request_token.is_cancelled())
                    co_return {};
                auto processed = co_await process_available();
                if (!processed)
                {
                    if (processed.error() == std::make_error_code(std::errc::resource_unavailable_try_again))
                    {
                        // The trailer HEADERS frame remains at the front of
                        // `wire`; wait for its QPACK inserts instead of
                        // consuming it and misclassifying a later DATA frame.
                        const auto progress = co_await qpack_progress_.receive();
                        if (progress)
                            continue;
                        request_token.cancel();
                        co_return std::unexpected(
                            std::make_error_code(std::errc::not_connected));
                    }
                    request_token.cancel();
                    co_return std::unexpected(processed.error());
                }
                auto received = co_await conn_.async_recv(
                    id, wire.prepare(stream_read_chunk_size));
                if (!received)
                {
                    if (received.error() ==
                        std::make_error_code(std::errc::operation_would_block))
                    {
                        auto ready = co_await conn_.async_wait_readable(
                            id, request_token);
                        if (!ready)
                        {
                            if (request_token.is_cancelled())
                                co_return {};
                            co_return std::unexpected(ready.error());
                        }
                        continue;
                    }
                    co_return std::unexpected(received.error());
                }
                if (*received == 0U)
                    break;
                wire.commit(*received);
            }
            auto processed = co_await process_available();
            if (!processed)
                co_return std::unexpected(processed.error());
            co_return {};
        };

        auto invoke_handler = [&]()
            -> task<std::expected<void, std::error_code>>
        {
            auto handled = co_await streaming_handler_(*request, response,
                *body_stream, request_token);
            // A handler that deliberately returns before draining the body
            // must not leave the receive coroutine parked forever.
            if (!body_stream->is_closed())
                request_token.cancel();
            co_return handled;
        };

        auto [handled, body_result] = co_await when_all(
            invoke_handler(), consume_body());

        if (!handled || !body_result)
        {
            if (!request_token.is_cancelled())
                response.status = status::internal_server_error;
            response.body.clear();
        }
    }
    else if (async_handler_)
    {

        cancel_token request_token;
        conn_.register_stream_cancellation(id, request_token);
        scope_guard unregister{[this, id]
            {
                conn_.unregister_stream_cancellation(id);
            }};
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

    // PUSH_PROMISE frames are part of the parent request stream and must
    // precede its final response HEADERS. Do this only after the handler has
    // produced its complete response so an invalid push never escapes as a
    // half-promise.
    const auto pushed = co_await send_pushes(id, response.pushes);
    if (!pushed)
    {
        if (pushed.error() == std::make_error_code(std::errc::operation_not_permitted))
        {
            // Server push is optional. A client that did not advertise a
            // limit receives the primary response unchanged.
            response.pushes.clear();
        }
        else
        {
            (void)co_await conn_.async_cancel_stream(id);
            co_return;
        }
    }

    // RFC 9114 carries HEAD responses on the same stream shape as any other
    // response, but a HEAD response never contains DATA. Keep headers (and an
    // explicitly supplied Content-Length) while suppressing either body form.
    const bool suppress_response_body = response_forbids_content(
        response.status, request->method == http_method::HEAD);
    if (suppress_response_body)
        response.body_source.reset();

    const auto declared_length = response_declared_length(response);
    if (!declared_length)
    {
        (void)co_await conn_.async_cancel_stream(id);
        co_return;
    }
    if (!suppress_response_body && !response.body_source && declared_length &&
        *declared_length && **declared_length != response.body.size())
    {
        (void)co_await conn_.async_cancel_stream(id);
        co_return;
    }

    if (response.body_source)
    {
        if (!response.body.empty())
        {
            (void)co_await conn_.async_cancel_stream(id);
            co_return;
        }

        cancel_token response_token;
        conn_.register_stream_cancellation(id, response_token);
        scope_guard response_unregister{[this, id]
            {
                conn_.unregister_stream_cancellation(id);
            }};
        const auto source = response.body_source;
        auto expected_length = source->content_length();
        if (!expected_length && declared_length)
            expected_length = *declared_length;
        if (expected_length && declared_length && *declared_length &&
            *expected_length != **declared_length)
        {
            (void)co_await conn_.async_cancel_stream(id);
            co_return;
        }
        std::uint64_t sent_length = 0U;

        auto cancel_response = [&]() -> task<void>
        {
            (void)co_await conn_.async_cancel_stream(id);
        };

        std::expected<void, std::error_code> response_sent;
        {
            co_await response_header_mutex_.lock();
            cnetmod::async_lock_guard header_guard{
                response_header_mutex_, std::adopt_lock};
            auto encoded = response_headers_frame(encoder_, response, id);
            if (!encoded)
            {
                co_await conn_.async_close(encoded.error(),
                    "HTTP/3 streaming response header encoding failed");
                co_return;
            }
            auto encoder_flush = co_await flush_qpack_encoder_instructions(
                conn_, encoder_, qpack_encoder_stream_);
            if (!encoder_flush)
            {
                co_await conn_.async_close(encoder_flush.error(),
                    "HTTP/3 QPACK encoder stream failed");
                co_return;
            }
            response_sent = co_await conn_.async_send(id, std::move(*encoded), false);
        }
        if (!response_sent)
            co_return;

        for (;;)
        {
            if (response_token.is_cancelled())
            {
                co_await cancel_response();
                co_return;
            }
            auto next = co_await source->next(response_token);
            if (!next)
            {
                if (response_token.is_cancelled())
                {
                    co_await cancel_response();
                    co_return;
                }
                break;
            }
            const auto chunk = next->view();
            if (chunk.empty())
                continue;
            if (expected_length &&
                (sent_length > *expected_length ||
                    chunk.size() > *expected_length - sent_length))
            {
                co_await cancel_response();
                co_return;
            }
            auto encoded = encode_http3_frame(data_frame{chunk});
            response_sent = co_await conn_.async_send(id, std::move(encoded), false);
            if (!response_sent)
                co_return;
            sent_length += chunk.size();
        }
        if (expected_length && sent_length != *expected_length)
        {
            co_await cancel_response();
            co_return;
        }

        if (!response.trailers.empty())
        {
            co_await response_header_mutex_.lock();
            cnetmod::async_lock_guard header_guard{
                response_header_mutex_, std::adopt_lock};
            auto encoded = response_trailers_frame(encoder_, response, id);
            if (!encoded)
            {
                co_await cancel_response();
                co_return;
            }
            auto encoder_flush = co_await flush_qpack_encoder_instructions(
                conn_, encoder_, qpack_encoder_stream_);
            if (!encoder_flush)
            {
                co_await cancel_response();
                co_return;
            }
            response_sent = co_await conn_.async_send(id, std::move(*encoded), true);
        }
        else
        {
            response_sent = co_await conn_.async_send(id, std::span<const std::byte>{}, true);
        }
        if (!response_sent)
            co_return;
        (void)conn_.retire_stream(id);
        co_return;
    }

    std::expected<void, std::error_code> response_sent;
    std::optional<byte_buffer> static_response;
    {
        co_await response_header_mutex_.lock();
        cnetmod::async_lock_guard header_guard{
            response_header_mutex_, std::adopt_lock};
        auto encoded = response_frames(encoder_, response, id, suppress_response_body);
        if (!encoded)
        {
            co_await conn_.async_close(encoded.error(),
                "HTTP/3 response encoding failed");
            co_return;
        }

        // The usual response is represented entirely by QPACK static-table
        // entries.  Its bytes no longer touch encoder state after
        // response_frames(), so do not serialize every handler behind the
        // packet-owner acknowledgement.  A dynamic-table update still keeps
        // the mutex across encoder-stream then request-stream submission: the
        // two writes must retain their RFC 9204 ordering.
        auto instructions = encoder_.take_encoder_instructions();
        if (instructions.empty())
        {
            static_response.emplace(std::move(*encoded));
        }
        else
        {
            if (!qpack_encoder_stream_)
            {
                co_await conn_.async_close(std::make_error_code(std::errc::protocol_error),
                    "HTTP/3 QPACK encoder stream unavailable");
                co_return;
            }
            const auto encoder_sent = co_await conn_.async_send(
                *qpack_encoder_stream_, std::move(instructions), false);
            if (!encoder_sent)
            {
                co_await conn_.async_close(encoder_sent.error(),
                    "HTTP/3 QPACK encoder stream failed");
                co_return;
            }
            response_sent = co_await conn_.async_send(id, std::move(*encoded), true);
        }
    }
    if (static_response)
        response_sent = co_await conn_.async_send(id, std::move(*static_response), true);
    if (response_sent)
        (void)conn_.retire_stream(id);
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

auto validate_peer_uni_stream_payload(quic_connection& connection,
    qpack_decoder& decoder, qpack_encoder& encoder,
    bool& control_seen, bool& settings_seen, bool& encoder_stream_seen,
    bool& decoder_stream_seen, bool& peer_datagram_enabled, bool& peer_connect_protocol_enabled,
    bool& peer_webtransport_enabled, std::optional<std::uint64_t>& peer_max_push_id,
    bool& received_goaway, std::uint64_t& goaway_stream_id,
    std::vector<std::uint64_t>* cancelled_push_ids,
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
                            identifier == static_cast<std::uint64_t>(http3_setting_key::h3_datagram) ||
                            identifier == static_cast<std::uint64_t>(http3_setting_key::enable_webtransport)) &&
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
                const auto datagram = settings->settings.find(
                    static_cast<std::uint64_t>(http3_setting_key::h3_datagram));
                peer_datagram_enabled = datagram != settings->settings.end() &&
                    std::get<std::uint64_t>(datagram->second) == 1U;
                const auto connect_protocol = settings->settings.find(
                    static_cast<std::uint64_t>(http3_setting_key::enable_connect_protocol));
                peer_connect_protocol_enabled = connect_protocol != settings->settings.end() &&
                    std::get<std::uint64_t>(connect_protocol->second) == 1U;
                const auto webtransport = settings->settings.find(
                    static_cast<std::uint64_t>(http3_setting_key::enable_webtransport));
                peer_webtransport_enabled = webtransport != settings->settings.end() &&
                    std::get<std::uint64_t>(webtransport->second) == 1U;
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
            else if (const auto* max_push = std::get_if<max_push_id_frame>(&frame->first))
            {
                // Only clients advertise MAX_PUSH_ID. The value is monotonic
                // (RFC 9114 §7.2.7); accepting a decrease could make an
                // already promised resource spuriously illegal.
                if (!settings_seen || peer_is_server ||
                    (peer_max_push_id && max_push->max_push_id < *peer_max_push_id))
                    return std::unexpected(std::make_error_code(std::errc::protocol_error));
                peer_max_push_id = max_push->max_push_id;
            }
            else if (const auto* update = std::get_if<priority_update_frame>(&frame->first))
            {
                // This implementation schedules request streams. Push-priority
                // element IDs require server-push support and are therefore
                // rejected rather than silently applying a wrong stream ID.
                if (!settings_seen || (update->prioritized_element_id & 0x03U) != 0U)
                    return std::unexpected(std::make_error_code(std::errc::protocol_error));
                const auto priority = parse_http_priority(update->priority_field_value);
                if (!priority)
                    return std::unexpected(priority.error());
                if (!connection.set_stream_priority(update->prioritized_element_id,
                        priority->urgency, priority->incremental))
                    return std::unexpected(
                        std::make_error_code(std::errc::resource_unavailable_try_again));
            }
            else if (const auto* cancel = std::get_if<cancel_push_frame>(&frame->first))
            {
                // RFC 9114 only permits a client to cancel a server push.
                // The owning server session applies the collected IDs after
                // this parser finishes its non-suspending transition.
                if (!settings_seen || peer_is_server || !cancelled_push_ids)
                    return std::unexpected(std::make_error_code(std::errc::protocol_error));
                cancelled_push_ids->push_back(cancel->push_id);
            }
            // SETTINGS is mandatory and must be the first control frame.
            else if (!settings_seen || std::holds_alternative<data_frame>(frame->first) ||
                std::holds_alternative<headers_frame>(frame->first) ||
                std::holds_alternative<push_promise_frame>(frame->first))
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

auto validate_peer_uni_stream(quic_connection& connection,
    qpack_decoder& decoder, qpack_encoder& encoder,
    bool& control_seen, bool& settings_seen, bool& encoder_stream_seen,
    bool& decoder_stream_seen, bool& peer_datagram_enabled, bool& peer_connect_protocol_enabled,
    bool& peer_webtransport_enabled, std::optional<std::uint64_t>& peer_max_push_id,
    bool& received_goaway, std::uint64_t& goaway_stream_id,
    std::vector<std::uint64_t>* cancelled_push_ids,
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
        auto result = validate_peer_uni_stream_payload(connection, decoder, encoder,
            control_seen, settings_seen, encoder_stream_seen, decoder_stream_seen, peer_datagram_enabled,
            peer_connect_protocol_enabled, peer_webtransport_enabled, peer_max_push_id,
            received_goaway, goaway_stream_id, cancelled_push_ids, peer_is_server, type->second,
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
    auto result = validate_peer_uni_stream_payload(connection, decoder, encoder,
        control_seen, settings_seen, encoder_stream_seen, decoder_stream_seen, peer_datagram_enabled,
        peer_connect_protocol_enabled, peer_webtransport_enabled, peer_max_push_id,
        received_goaway, goaway_stream_id, cancelled_push_ids, peer_is_server, *type,
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
    co_await control_stream_mutex_.lock();
    cnetmod::async_lock_guard guard{control_stream_mutex_, std::adopt_lock};
    (void)co_await conn_.async_send(*control_stream_, frame, false);
}

auto http3_server_session::update_priority(stream_id request_stream,
    http_priority priority) -> task<std::expected<void, std::error_code>>
{
    co_return co_await send_priority_update(conn_, control_stream_,
        control_stream_mutex_, published_priorities_, request_stream, priority);
}

auto http3_server_session::apply_push_cancellations(
    const std::vector<std::uint64_t>& ids) -> task<void>
{
    if (ids.empty())
        co_return;
    co_await push_cancellation_mutex_.lock();
    cnetmod::async_lock_guard guard{push_cancellation_mutex_, std::adopt_lock};
    for (const auto id : ids)
    {
        if (const auto active = active_push_cancellations_.find(id);
            active != active_push_cancellations_.end())
            active->second->cancel();
        else
            peer_cancelled_pushes_.insert_or_assign(id, true);
        if (push_cancellation_observer_)
        {
            try
            {
                push_cancellation_observer_(id);
            }
            catch (...)
            {
                // Observability must never alter HTTP/3 protocol behavior.
            }
        }
    }
}

auto http3_server_session::register_push_cancellation(std::uint64_t id,
    const std::shared_ptr<cnetmod::cancel_token>& token) -> task<void>
{
    co_await push_cancellation_mutex_.lock();
    cnetmod::async_lock_guard guard{push_cancellation_mutex_, std::adopt_lock};
    if (peer_cancelled_pushes_.erase(id) != 0U)
        token->cancel();
    else
        active_push_cancellations_.insert_or_assign(id, token);
}

auto http3_server_session::release_push_cancellation(std::uint64_t id)
    -> task<void>
{
    co_await push_cancellation_mutex_.lock();
    cnetmod::async_lock_guard guard{push_cancellation_mutex_, std::adopt_lock};
    active_push_cancellations_.erase(id);
    peer_cancelled_pushes_.erase(id);
}

auto http3_server_session::send_pushes(stream_id parent_stream,
    const std::vector<http3_push>& pushes)
    -> task<std::expected<void, std::error_code>>
{
    if (pushes.empty())
        co_return {};

    for (const auto& push : pushes)
    {
        if (!push.response ||
            (!push.response->body.empty() && push.response->body_source))
            co_return std::unexpected(std::make_error_code(std::errc::invalid_argument));

        std::uint64_t push_id{};
        {
            // QPACK is connection-scoped, but an application body source can
            // await arbitrary I/O. Keep the serialization window to promise
            // and header encoding only so it cannot stall unrelated streams.
            co_await response_header_mutex_.lock();
            cnetmod::async_lock_guard header_guard{
                response_header_mutex_, std::adopt_lock};
            if (!peer_max_push_id_)
                co_return std::unexpected(
                    std::make_error_code(std::errc::operation_not_permitted));
            if (next_push_id_ > *peer_max_push_id_)
                co_return std::unexpected(std::make_error_code(std::errc::invalid_argument));
            push_id = next_push_id_++;
            auto promise_block = encoder_.encode(headers_for(push.request), parent_stream);
            if (!promise_block)
                co_return std::unexpected(promise_block.error());
            const auto promise = encode_http3_frame(push_promise_frame{
                push_id, *promise_block});
            const auto encoder_flush = co_await flush_qpack_encoder_instructions(
                conn_, encoder_, qpack_encoder_stream_);
            if (!encoder_flush)
                co_return std::unexpected(encoder_flush.error());
            const auto promise_sent = co_await conn_.async_send(parent_stream,
                promise, false);
            if (!promise_sent)
                co_return std::unexpected(promise_sent.error());
        }

        const auto stream = co_await conn_.async_open_stream(false);
        if (!stream)
            co_return std::unexpected(stream.error());
        if (push.priority && !conn_.set_stream_priority(*stream, push.priority->urgency, push.priority->incremental))
            co_return std::unexpected(
                std::make_error_code(std::errc::resource_unavailable_try_again));

        byte_buffer preface;
        append_varint(push_stream_type, preface);
        append_varint(push_id, preface);
        const auto retire_push = scope_guard{[this, stream]
            {
                (void)conn_.retire_stream(*stream);
            }};
        const bool suppress_body = response_forbids_content(push.response->status,
            push.request.method == http_method::HEAD);
        if (!push.response->body_source || suppress_body)
        {
            std::expected<byte_buffer, std::error_code> frames;
            {
                co_await response_header_mutex_.lock();
                cnetmod::async_lock_guard header_guard{
                    response_header_mutex_, std::adopt_lock};
                frames = response_frames(encoder_, *push.response, *stream,
                    suppress_body);
                if (!frames)
                    co_return std::unexpected(frames.error());
                const auto encoder_flush = co_await flush_qpack_encoder_instructions(
                    conn_, encoder_, qpack_encoder_stream_);
                if (!encoder_flush)
                    co_return std::unexpected(encoder_flush.error());
            }
            preface.insert(preface.end(), frames->begin(), frames->end());
            const auto sent = co_await conn_.async_send(*stream, preface, true);
            if (!sent)
                co_return std::unexpected(sent.error());
            continue;
        }

        const auto declared_length = response_declared_length(*push.response);
        if (!declared_length)
            co_return std::unexpected(declared_length.error());
        auto expected_length = push.response->body_source->content_length();
        if (!expected_length && *declared_length)
            expected_length = **declared_length;
        if (expected_length && *declared_length && *expected_length != **declared_length)
            co_return std::unexpected(std::make_error_code(std::errc::invalid_argument));

        std::expected<byte_buffer, std::error_code> headers;
        {
            co_await response_header_mutex_.lock();
            cnetmod::async_lock_guard header_guard{
                response_header_mutex_, std::adopt_lock};
            headers = response_headers_frame(encoder_, *push.response, *stream);
            if (!headers)
                co_return std::unexpected(headers.error());
            const auto encoder_flush = co_await flush_qpack_encoder_instructions(
                conn_, encoder_, qpack_encoder_stream_);
            if (!encoder_flush)
                co_return std::unexpected(encoder_flush.error());
        }
        preface.insert(preface.end(), headers->begin(), headers->end());
        const auto sent_headers = co_await conn_.async_send(*stream, preface, false);
        if (!sent_headers)
            co_return std::unexpected(sent_headers.error());

        auto token = std::make_shared<cancel_token>();
        co_await register_push_cancellation(push_id, token);
        // The body source can wait on a database, cache, or another
        // coroutine.  Do not await it in the parent response path: doing so
        // postpones the parent HEADERS and makes CANCEL_PUSH impossible to
        // issue before DATA has already been emitted.
        spawn(conn_.context(), send_push_body(push_id, *stream, push.response, expected_length, std::move(token)));
    }
    co_return {};
}

auto http3_server_session::send_push_body(std::uint64_t push_id,
    stream_id stream, std::shared_ptr<http3_response> response,
    std::optional<std::uint64_t> expected_length,
    std::shared_ptr<cancel_token> token) -> task<void>
{
    conn_.register_stream_cancellation(stream, *token);
    const auto unregister = scope_guard{[this, stream]
        {
            conn_.unregister_stream_cancellation(stream);
        }};
    const auto release = scope_guard{[this, push_id]
        {
            spawn(conn_.context(), release_push_cancellation(push_id));
        }};

    std::uint64_t sent_length{};
    for (;;)
    {
        if (token->is_cancelled())
        {
            (void)co_await conn_.async_cancel_stream(stream);
            co_return;
        }
        auto next = co_await response->body_source->next(*token);
        if (!next)
            break;
        const auto chunk = next->view();
        if (chunk.empty())
            continue;
        if (expected_length && (sent_length > *expected_length || chunk.size() > *expected_length - sent_length))
        {
            (void)co_await conn_.async_cancel_stream(stream);
            co_return;
        }
        const auto sent = co_await conn_.async_send(stream,
            encode_http3_frame(data_frame{chunk}), false);
        if (!sent)
            co_return;
        sent_length += chunk.size();
    }
    if (token->is_cancelled() ||
        (expected_length && sent_length != *expected_length))
    {
        (void)co_await conn_.async_cancel_stream(stream);
        co_return;
    }
    if (response->trailers.empty())
    {
        (void)co_await conn_.async_send(stream, std::span<const std::byte>{}, true);
        co_return;
    }

    std::expected<byte_buffer, std::error_code> trailers;
    {
        co_await response_header_mutex_.lock();
        cnetmod::async_lock_guard header_guard{
            response_header_mutex_, std::adopt_lock};
        trailers = response_trailers_frame(encoder_, *response, stream);
        if (!trailers)
        {
            (void)co_await conn_.async_cancel_stream(stream);
            co_return;
        }
        const auto encoder_flush = co_await flush_qpack_encoder_instructions(
            conn_, encoder_, qpack_encoder_stream_);
        if (!encoder_flush)
        {
            (void)co_await conn_.async_cancel_stream(stream);
            co_return;
        }
    }
    (void)co_await conn_.async_send(stream, *trailers, true);
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

auto http3_client_session::configure_server_push(
    std::optional<std::uint64_t> max_push_id, server_push_handler handler,
    server_push_promise_handler promise_handler) -> void
{
    // A callback without a limit must not accidentally authorize pushes.
    local_max_push_id_ = max_push_id;
    push_handler_ = std::move(handler);
    push_promise_handler_ = std::move(promise_handler);
    if (!local_max_push_id_)
    {
        push_handler_ = {};
        push_promise_handler_ = {};
    }
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
    if (local_max_push_id_)
    {
        const auto advertised = co_await conn_.async_send(*control_stream_,
            encode_http3_frame(max_push_id_frame{*local_max_push_id_}), false);
        if (!advertised)
            co_return std::unexpected(advertised.error());
    }
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

auto http3_client_session::update_priority(stream_id request_stream,
    http_priority priority) -> task<std::expected<void, std::error_code>>
{
    co_return co_await send_priority_update(conn_, control_stream_,
        control_stream_mutex_, published_priorities_, request_stream, priority);
}

auto http3_client_session::cancel_server_push(std::uint64_t push_id)
    -> task<std::expected<void, std::error_code>>
{
    if (!local_max_push_id_ || push_id > *local_max_push_id_)
        co_return std::unexpected(std::make_error_code(std::errc::invalid_argument));

    // The promised-push and cancelled-push maps share qpack_mutex_ with the
    // PUSH_PROMISE parser.  Publish local suppression before putting the
    // control frame on the wire so an already buffered Push stream cannot
    // race through to the completion handler.
    co_await qpack_mutex_.lock();
    cnetmod::async_lock_guard qpack_guard{qpack_mutex_, std::adopt_lock};
    if (cancelled_pushes_.contains(push_id))
        co_return {};
    if (!promised_pushes_.contains(push_id))
        co_return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    cancelled_pushes_.insert_or_assign(push_id, true);
    qpack_guard.release();
    qpack_mutex_.unlock();

    co_await control_stream_mutex_.lock();
    cnetmod::async_lock_guard control_guard{control_stream_mutex_, std::adopt_lock};
    if (!control_stream_)
        co_return std::unexpected(std::make_error_code(std::errc::not_connected));
    const auto sent = co_await conn_.async_send(*control_stream_,
        encode_http3_frame(cancel_push_frame{push_id}), false);
    if (!sent)
    {
        control_guard.release();
        control_stream_mutex_.unlock();
        co_await qpack_mutex_.lock();
        cnetmod::async_lock_guard rollback{qpack_mutex_, std::adopt_lock};
        cancelled_pushes_.erase(push_id);
        co_return std::unexpected(sent.error());
    }
    co_return {};
}

auto http3_client_session::dispatch_server_push_promises(stream_id parent_stream)
    -> task<void>
{
    std::vector<server_push_promise> pending;
    {
        co_await qpack_mutex_.lock();
        cnetmod::async_lock_guard qpack_guard{qpack_mutex_, std::adopt_lock};
        for (const auto& [push_id, request] : promised_pushes_)
        {
            if (request.request_stream != parent_stream ||
                notified_pushes_.contains(push_id) ||
                cancelled_pushes_.contains(push_id))
                continue;
            notified_pushes_.insert_or_assign(push_id, true);
            pending.push_back({push_id, request});
        }
    }
    if (!push_promise_handler_)
        co_return;
    for (auto& promise : pending)
    {
        const auto push_id = promise.push_id;
        const auto accepted = co_await push_promise_handler_(std::move(promise));
        if (!accepted)
        {
            const auto cancelled = co_await cancel_server_push(push_id);
            if (!cancelled)
                logger::warn{"HTTP/3 could not send CANCEL_PUSH {}: {} ({})",
                    push_id, cancelled.error().message(), cancelled.error().value()};
        }
    }
}

auto http3_client_session::send_request(const http3_request& request)
    -> task<std::expected<http3_response, std::error_code>>
{
    if (request.body_source)
    {
        cnetmod::cancel_token token;
        co_return co_await send_request(request, token);
    }
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
    if (request.priority)
    {
        const auto updated = co_await update_priority(*stream, *request.priority);
        if (!updated)
        {
            (void)co_await conn_.async_cancel_stream(*stream);
            co_return std::unexpected(updated.error());
        }
    }
    std::expected<void, std::error_code> sent;
    {
        co_await request_header_mutex_.lock();
        cnetmod::async_lock_guard header_guard{
            request_header_mutex_, std::adopt_lock};
        auto block = encoder_.encode(headers_for(request), *stream);
        if (!block)
            co_return std::unexpected(block.error());
        auto encoder_flush = co_await flush_qpack_encoder_instructions(
            conn_, encoder_, qpack_encoder_stream_);
        if (!encoder_flush)
            co_return std::unexpected(encoder_flush.error());
        auto headers = encode_http3_frame(headers_frame{*block});
        sent = co_await conn_.async_send(*stream, headers,
            request.body.empty() && !request.body_source && request.trailers.empty());
    }
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
        {
            co_await request_header_mutex_.lock();
            cnetmod::async_lock_guard header_guard{
                request_header_mutex_, std::adopt_lock};
            auto trailer_block = encoder_.encode(trailers, *stream);
            if (!trailer_block)
                co_return std::unexpected(trailer_block.error());
            auto encoder_flush = co_await flush_qpack_encoder_instructions(
                conn_, encoder_, qpack_encoder_stream_);
            if (!encoder_flush)
                co_return std::unexpected(encoder_flush.error());
            auto trailer_frame = encode_http3_frame(headers_frame{*trailer_block});
            sent = co_await conn_.async_send(*stream, trailer_frame, true);
        }
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

    auto parse_response = [&]() -> task<std::expected<http3_response, std::error_code>>
    {
        co_await qpack_mutex_.lock();
        cnetmod::async_lock_guard qpack_guard{qpack_mutex_, std::adopt_lock};
        auto& completed_headers = completed_headers_[*stream];
        co_return response_from_frames(decoder_, wire.readable_view(), *stream,
            completed_headers, &promised_pushes_,
            local_max_push_id_ ? &local_max_push_id_ : nullptr,
            &push_promise_progress_);
    };
    auto response = co_await parse_response();
    while (!response && response.error() == std::make_error_code(std::errc::resource_unavailable_try_again))
    {
        const auto progress = co_await qpack_progress_.receive();
        if (!progress)
            co_return std::unexpected(std::make_error_code(std::errc::not_connected));
        response = co_await parse_response();
    }
    completed_headers_.erase(*stream);
    if (!response)
        co_return std::unexpected(response.error());

    co_await dispatch_server_push_promises(*stream);

    auto decoder_flush = co_await flush_qpack_decoder_instructions(conn_, decoder_, qpack_decoder_stream_);
    if (!decoder_flush)
        co_return std::unexpected(decoder_flush.error());
    (void)conn_.retire_stream(*stream);

    co_return *response;
}

auto http3_client_session::connect_webtransport(const http3_request& request)
    -> task<std::expected<webtransport_session, std::error_code>>
{
    if (received_goaway_)
        co_return std::unexpected(std::make_error_code(std::errc::connection_aborted));
    if (!settings_.enable_connect_protocol || !settings_.enable_datagram ||
        request.method != http_method::CONNECT || request.protocol != "webtransport" ||
        !request.body.empty() || !request.trailers.empty())
        co_return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    auto connected = co_await connect();
    if (!connected)
        co_return std::unexpected(connected.error());

    const auto stream = co_await conn_.async_open_stream(true);
    if (!stream)
        co_return std::unexpected(stream.error());
    if (request.priority)
    {
        const auto updated = co_await update_priority(*stream, *request.priority);
        if (!updated)
        {
            (void)co_await conn_.async_cancel_stream(*stream);
            co_return std::unexpected(updated.error());
        }
    }
    const auto block = encoder_.encode(headers_for(request), *stream);
    if (!block)
        co_return std::unexpected(block.error());
    const auto encoder_flush = co_await flush_qpack_encoder_instructions(
        conn_, encoder_, qpack_encoder_stream_);
    if (!encoder_flush)
        co_return std::unexpected(encoder_flush.error());
    const auto headers = encode_http3_frame(headers_frame{*block});
    const auto sent = co_await conn_.async_send(*stream, headers, false);
    if (!sent)
        co_return std::unexpected(sent.error());

    dynamic_buffer wire{stream_read_chunk_size};
    for (;;)
    {
        const auto received = co_await conn_.async_recv(*stream,
            wire.prepare(stream_read_chunk_size));
        if (!received)
        {
            if (received.error() != std::make_error_code(std::errc::operation_would_block))
                co_return std::unexpected(received.error());
            const auto ready = co_await conn_.async_wait_readable(*stream);
            if (!ready)
                co_return std::unexpected(ready.error());
            continue;
        }
        if (*received == 0U)
            co_return std::unexpected(std::make_error_code(std::errc::connection_aborted));
        wire.commit(*received);
        auto& completed_headers = completed_headers_[*stream];
        auto response = response_from_frames(decoder_, wire.readable_view(),
            *stream, completed_headers, &promised_pushes_,
            local_max_push_id_ ? &local_max_push_id_ : nullptr,
            &push_promise_progress_);
        if (!response)
        {
            if (response.error() == std::make_error_code(std::errc::message_size))
                continue;
            if (response.error() == std::make_error_code(std::errc::resource_unavailable_try_again))
            {
                const auto progress = co_await qpack_progress_.receive();
                if (progress)
                    continue;
                co_return std::unexpected(std::make_error_code(std::errc::not_connected));
            }
            co_return std::unexpected(response.error());
        }
        completed_headers_.erase(*stream);
        co_await dispatch_server_push_promises(*stream);
        const auto decoder_flush = co_await flush_qpack_decoder_instructions(
            conn_, decoder_, qpack_decoder_stream_);
        if (!decoder_flush)
            co_return std::unexpected(decoder_flush.error());
        if (response->status < 200 || response->status >= 300)
        {
            (void)co_await conn_.async_cancel_stream(*stream);
            co_return std::unexpected(std::make_error_code(std::errc::connection_refused));
        }
        webtransport_session session{conn_, *stream};
        webtransport_sessions_.insert_or_assign(*stream, session.state());
        spawn(conn_.context(), consume_webtransport_close_capsules(conn_, session.state()));
        if (!datagram_dispatcher_started_)
        {
            datagram_dispatcher_started_ = true;
            spawn(conn_.context(), dispatch_datagrams());
        }
        co_return session;
    }
}

auto http3_server_session::datagrams_enabled() const noexcept -> bool
{
    return local_settings_.enable_datagram && peer_settings_seen_ && peer_datagram_enabled_;
}

auto http3_server_session::webtransport_enabled() const noexcept -> bool
{
    return datagrams_enabled() && local_settings_.enable_connect_protocol &&
        local_settings_.enable_webtransport && peer_connect_protocol_enabled_ &&
        peer_webtransport_enabled_;
}

auto http3_server_session::route_webtransport_stream(stream_id id, byte_view bytes,
    bool unidirectional) -> std::expected<bool, std::error_code>
{
    if (!unidirectional)
    {
        auto session_id = quic::decode_varint(bytes);
        if (!session_id)
        {
            if (session_id.error() == std::make_error_code(std::errc::bad_message))
                return std::unexpected(std::make_error_code(std::errc::message_size));
            return false;
        }
        if (session_id->first == webtransport_legacy_bidirectional_stream_type)
        {
            session_id = quic::decode_varint(bytes.subspan(session_id->second));
            if (!session_id)
                return std::unexpected(std::make_error_code(std::errc::message_size));
        }
        // A valid WebTransport child preface can arrive before the CONNECT
        // coroutine publishes its session in `webtransport_sessions_`.  Keep
        // the stream unclassified and let the caller wait for registration;
        // treating it as an HTTP request permanently loses the preface.
        if (is_webtransport_session_id(session_id->first) &&
            !webtransport_sessions_.contains(static_cast<stream_id>(session_id->first)))
            return std::unexpected(std::make_error_code(std::errc::resource_unavailable_try_again));
        const auto session = webtransport_sessions_.find(static_cast<stream_id>(session_id->first));
        if (session == webtransport_sessions_.end())
            return false;
        if (!is_webtransport_session_id(session_id->first))
            return std::unexpected(std::make_error_code(std::errc::protocol_error));
        session->second->owned_streams.insert(id);
        if (!session->second->streams.try_send(id))
        {
            session->second->owned_streams.erase(id);
            return std::unexpected(std::make_error_code(std::errc::protocol_error));
        }

        return true;
    }
    const auto type = quic::decode_varint(bytes);
    if (!type)
        return std::unexpected(std::make_error_code(std::errc::message_size));
    if (type->first != webtransport_unidirectional_stream_type)
        return false;
    const auto session_id = quic::decode_varint(bytes.subspan(type->second));
    if (!session_id)
        return std::unexpected(std::make_error_code(std::errc::message_size));
    if (!webtransport_sessions_.contains(static_cast<stream_id>(session_id->first)))
        return std::unexpected(std::make_error_code(std::errc::resource_unavailable_try_again));
    const auto session = webtransport_sessions_.find(static_cast<stream_id>(session_id->first));
    if (session == webtransport_sessions_.end() || !is_webtransport_session_id(session_id->first))
    {
        return std::unexpected(std::make_error_code(std::errc::protocol_error));
    }
    session->second->owned_streams.insert(id);
    if (!session->second->streams.try_send(id))
    {
        session->second->owned_streams.erase(id);
        return std::unexpected(std::make_error_code(std::errc::protocol_error));
    }

    return true;
}

auto http3_server_session::send_datagram(std::uint64_t context_id, byte_view payload)
    -> task<std::expected<void, std::error_code>>
{
    if (!datagrams_enabled())
        co_return std::unexpected(std::make_error_code(std::errc::not_supported));
    const auto wire = encode_http_datagram({context_id, payload});
    co_return co_await conn_.async_send_datagram(wire);
}

auto http3_server_session::receive_datagram()
    -> task<std::expected<std::pair<std::uint64_t, std::vector<std::byte>>, std::error_code>>
{
    if (!datagrams_enabled())
        co_return std::unexpected(std::make_error_code(std::errc::not_supported));
    if (!datagram_dispatcher_started_)
    {
        datagram_dispatcher_started_ = true;
        spawn(conn_.context(), dispatch_datagrams());
    }
    auto datagram = co_await http_datagrams_.receive();
    if (!datagram)
        co_return std::unexpected(std::make_error_code(std::errc::not_connected));
    co_return std::move(*datagram);
}

auto http3_server_session::dispatch_datagrams() -> task<void>
{
    while (!closing_ && !conn_.is_closed())
    {
        auto wire = co_await conn_.async_receive_datagram();
        if (!wire)
            break;
        auto decoded = decode_http_datagram(byte_view{wire->data(), wire->size()});
        if (!decoded)
            continue;
        std::vector<std::byte> payload(decoded->payload.begin(), decoded->payload.end());
        const auto session_id = webtransport_session_id_from_datagram_context(decoded->context_id);
        if (session_id)
        {
            if (const auto session = webtransport_sessions_.find(*session_id);
                session != webtransport_sessions_.end() && !session->second->closed.load())
            {
                (void)session->second->datagrams.try_send(std::move(payload));
                continue;
            }
        }
        (void)http_datagrams_.try_send(
            std::pair{decoded->context_id, std::move(payload)});
    }
    http_datagrams_.close();
    for (auto& [_, session] : webtransport_sessions_)
        session->datagrams.close();
}

auto http3_client_session::send_request(const http3_request& request,
    cnetmod::cancel_token& token)
    -> task<std::expected<http3_response, std::error_code>>
{

    co_await request_mutex_.lock();
    cnetmod::async_lock_guard request_guard{request_mutex_, std::adopt_lock};

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
    if (request.priority)
    {
        const auto updated = co_await update_priority(*stream, *request.priority);
        if (!updated)
        {
            (void)co_await conn_.async_cancel_stream(*stream);
            co_return std::unexpected(updated.error());
        }
    }

    auto cancel_stream = [&]() -> task<void>
    {
        (void)co_await conn_.async_cancel_stream(*stream);
    };
    std::expected<void, std::error_code> sent;
    {
        co_await request_header_mutex_.lock();
        cnetmod::async_lock_guard header_guard{
            request_header_mutex_, std::adopt_lock};
        auto block = encoder_.encode(headers_for(request), *stream);
        if (!block)
            co_return std::unexpected(block.error());
        auto encoder_flush = co_await flush_qpack_encoder_instructions(
            conn_, encoder_, qpack_encoder_stream_);
        if (!encoder_flush)
            co_return std::unexpected(encoder_flush.error());
        auto headers = encode_http3_frame(headers_frame{*block});
        sent = co_await conn_.async_send(*stream, headers,
            request.body.empty() && !request.body_source && request.trailers.empty());
    }
    if (!sent)
        co_return std::unexpected(sent.error());

    if (token.is_cancelled())
    {
        co_await cancel_stream();
        co_return std::unexpected(cnetmod::make_error_code(cnetmod::errc::operation_aborted));
    }
    if (token.is_cancelled())
    {
        co_await cancel_stream();
        co_return std::unexpected(cnetmod::make_error_code(cnetmod::errc::operation_aborted));
    }
    if (request.body_source)
    {
        const auto expected_length = request.body_source->content_length();
        std::uint64_t sent_length = 0;
        std::vector<std::byte> pending_chunk;
        auto send_pending = [&](bool fin) -> task<std::expected<void, std::error_code>>
        {
            if (pending_chunk.empty())
                co_return {};
            if (expected_length &&
                (sent_length > *expected_length ||
                    pending_chunk.size() > *expected_length - sent_length))
                co_return std::unexpected(make_error_code(http_errc::body_too_large));
            auto data = encode_http3_frame(
                data_frame{std::span<const std::byte>{pending_chunk.data(), pending_chunk.size()}});
            auto result = co_await conn_.async_send(*stream, data, fin);
            if (!result)
                co_return std::unexpected(result.error());
            sent_length += pending_chunk.size();
            pending_chunk.clear();
            co_return {};
        };
        for (;;)
        {
            if (token.is_cancelled())
            {
                co_await cancel_stream();
                co_return std::unexpected(cnetmod::make_error_code(cnetmod::errc::operation_aborted));
            }
            auto next = co_await request.body_source->next(token);
            if (!next)
            {
                if (token.is_cancelled())
                {
                    co_await cancel_stream();
                    co_return std::unexpected(cnetmod::make_error_code(cnetmod::errc::operation_aborted));
                }
                break;
            }
            const auto chunk = next->view();
            if (chunk.empty())
                continue;
            if (!pending_chunk.empty())
            {
                auto flushed = co_await send_pending(false);
                if (!flushed)
                {
                    co_await cancel_stream();
                    co_return std::unexpected(flushed.error());
                }
            }
            pending_chunk.assign(chunk.begin(), chunk.end());
        }
        if (!pending_chunk.empty())
        {
            auto flushed = co_await send_pending(request.trailers.empty());
            if (!flushed)
            {
                co_await cancel_stream();
                co_return std::unexpected(flushed.error());
            }
        }
        if (expected_length && sent_length != *expected_length)
        {
            co_await cancel_stream();
            co_return std::unexpected(make_error_code(http_errc::incomplete_message));
        }
        if (request.trailers.empty() && sent_length == 0)
        {
            sent = co_await conn_.async_send(*stream, std::span<const std::byte>{}, true);
            if (!sent)
                co_return std::unexpected(sent.error());
        }
    }
    else if (!request.body.empty())
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
        {
            co_await request_header_mutex_.lock();
            cnetmod::async_lock_guard header_guard{
                request_header_mutex_, std::adopt_lock};
            auto trailer_block = encoder_.encode(trailers, *stream);
            if (!trailer_block)
                co_return std::unexpected(trailer_block.error());
            auto encoder_flush = co_await flush_qpack_encoder_instructions(
                conn_, encoder_, qpack_encoder_stream_);
            if (!encoder_flush)
                co_return std::unexpected(encoder_flush.error());
            auto trailer_frame = encode_http3_frame(headers_frame{*trailer_block});
            sent = co_await conn_.async_send(*stream, trailer_frame, true);
        }
        if (!sent)
            co_return std::unexpected(sent.error());
        if (token.is_cancelled())
        {
            co_await cancel_stream();
            co_return std::unexpected(cnetmod::make_error_code(cnetmod::errc::operation_aborted));
        }
    }

    // The QUIC writer and QPACK encoder are protected during request
    // submission. Once the request FIN is on the wire, response reads are
    // independent streams and must not hold the write gate; releasing it here
    // allows HTTP/3 batch requests to receive concurrently.
    request_guard.release();
    request_mutex_.unlock();

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

    auto parse_response = [&]() -> task<std::expected<http3_response, std::error_code>>
    {
        co_await qpack_mutex_.lock();
        cnetmod::async_lock_guard qpack_guard{qpack_mutex_, std::adopt_lock};
        auto& completed_headers = completed_headers_[*stream];
        co_return response_from_frames(decoder_, wire.readable_view(), *stream,
            completed_headers, &promised_pushes_,
            local_max_push_id_ ? &local_max_push_id_ : nullptr,
            &push_promise_progress_);
    };
    auto response = co_await parse_response();
    while (!response && response.error() == std::make_error_code(std::errc::resource_unavailable_try_again))
    {
        const auto progress = co_await wait_for_qpack_progress(qpack_progress_, token);
        if (!progress)
        {
            if (token.is_cancelled())
                co_await cancel_stream();
            co_return std::unexpected(progress.error());
        }
        response = co_await parse_response();
    }
    completed_headers_.erase(*stream);
    if (!response)
        co_return std::unexpected(response.error());

    co_await dispatch_server_push_promises(*stream);

    auto decoder_flush = co_await flush_qpack_decoder_instructions(conn_, decoder_, qpack_decoder_stream_);
    if (!decoder_flush)
        co_return std::unexpected(decoder_flush.error());
    (void)conn_.retire_stream(*stream);

    co_return *response;
}

auto http3_client_session::send_request_streaming(const http3_request& request,
    streaming_client_response_handler handler)
    -> task<std::expected<http3_response, std::error_code>>
{
    cnetmod::cancel_token token;
    co_return co_await send_request_streaming(request, std::move(handler), token);
}

auto http3_client_session::send_request_streaming(const http3_request& request,
    streaming_client_response_handler handler, cnetmod::cancel_token& token)
    -> task<std::expected<http3_response, std::error_code>>
{
    if (!handler)
        co_return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    co_await request_mutex_.lock();
    cnetmod::async_lock_guard request_guard{request_mutex_, std::adopt_lock};

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

    if (request.priority)
    {
        const auto updated = co_await update_priority(*stream, *request.priority);
        if (!updated)
        {
            (void)co_await conn_.async_cancel_stream(*stream);
            co_return std::unexpected(updated.error());
        }
    }
    auto cancel_stream = [&]() -> task<void>
    {
        (void)co_await conn_.async_cancel_stream(*stream);
    };
    std::expected<void, std::error_code> sent;
    {
        co_await request_header_mutex_.lock();
        cnetmod::async_lock_guard header_guard{request_header_mutex_, std::adopt_lock};
        auto block = encoder_.encode(headers_for(request), *stream);
        if (!block)
            co_return std::unexpected(block.error());
        auto encoder_flush = co_await flush_qpack_encoder_instructions(
            conn_, encoder_, qpack_encoder_stream_);
        if (!encoder_flush)
            co_return std::unexpected(encoder_flush.error());
        auto headers = encode_http3_frame(headers_frame{*block});
        sent = co_await conn_.async_send(*stream, headers,
            request.body.empty() && !request.body_source && request.trailers.empty());
    }
    if (!sent)
        co_return std::unexpected(sent.error());

    if (token.is_cancelled())
    {
        co_await cancel_stream();
        co_return std::unexpected(cnetmod::make_error_code(cnetmod::errc::operation_aborted));
    }
    if (request.body_source)
    {
        const auto expected_length = request.body_source->content_length();
        std::uint64_t sent_length = 0;
        std::vector<std::byte> pending_chunk;
        auto send_pending = [&](bool fin) -> task<std::expected<void, std::error_code>>
        {
            if (pending_chunk.empty())
                co_return {};
            if (expected_length &&
                (sent_length > *expected_length ||
                    pending_chunk.size() > *expected_length - sent_length))
                co_return std::unexpected(make_error_code(http_errc::body_too_large));
            auto data = encode_http3_frame(data_frame{
                std::span<const std::byte>{pending_chunk.data(), pending_chunk.size()}});
            auto result = co_await conn_.async_send(*stream, data, fin);
            if (!result)
                co_return std::unexpected(result.error());
            sent_length += pending_chunk.size();
            pending_chunk.clear();
            co_return {};
        };
        for (;;)
        {
            if (token.is_cancelled())
            {
                co_await cancel_stream();
                co_return std::unexpected(cnetmod::make_error_code(cnetmod::errc::operation_aborted));
            }
            auto next = co_await request.body_source->next(token);
            if (!next)
            {
                if (token.is_cancelled())
                {
                    co_await cancel_stream();
                    co_return std::unexpected(cnetmod::make_error_code(
                        cnetmod::errc::operation_aborted));
                }
                break;
            }
            if (next->empty())
                continue;
            if (!pending_chunk.empty())
            {
                auto flushed = co_await send_pending(false);
                if (!flushed)
                {
                    co_await cancel_stream();
                    co_return std::unexpected(flushed.error());
                }
            }
            pending_chunk.assign(next->begin(), next->end());
        }
        if (!pending_chunk.empty())
        {
            auto flushed = co_await send_pending(request.trailers.empty());
            if (!flushed)
            {
                co_await cancel_stream();
                co_return std::unexpected(flushed.error());
            }
        }
        if (expected_length && sent_length != *expected_length)
        {
            co_await cancel_stream();
            co_return std::unexpected(make_error_code(http_errc::incomplete_message));
        }
        if (request.trailers.empty() && sent_length == 0U)
        {
            sent = co_await conn_.async_send(*stream, std::span<const std::byte>{}, true);
            if (!sent)
                co_return std::unexpected(sent.error());
        }
    }
    else if (!request.body.empty())
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
        co_await request_header_mutex_.lock();
        cnetmod::async_lock_guard header_guard{request_header_mutex_, std::adopt_lock};
        auto trailer_block = encoder_.encode(trailers, *stream);
        if (!trailer_block)
            co_return std::unexpected(trailer_block.error());
        auto encoder_flush = co_await flush_qpack_encoder_instructions(
            conn_, encoder_, qpack_encoder_stream_);
        if (!encoder_flush)
            co_return std::unexpected(encoder_flush.error());
        sent = co_await conn_.async_send(*stream,
            encode_http3_frame(headers_frame{*trailer_block}), true);
        if (!sent)
            co_return std::unexpected(sent.error());
    }

    request_guard.release();
    request_mutex_.unlock();

    http3_response response;
    response.version = http_version::http_3;
    response.body_stream = std::make_shared<request_body_stream>();
    channel<std::expected<void, std::error_code>> handler_done{1};
    auto handler_task = [&]() -> task<void>
    {
        auto result = co_await handler(response, *response.body_stream, token);
        response.body_stream->close();
        (void)handler_done.try_send(std::move(result));
    };

    dynamic_buffer wire{stream_read_chunk_size};
    std::size_t consumed{};
    bool headers_seen{};
    bool trailers_seen{};
    bool status_seen{};
    bool handler_started{};
    bool peer_fin{};
    std::uint64_t body_length{};
    auto declared_length = std::optional<std::uint64_t>{};
    std::error_code parse_error;
    std::optional<std::expected<void, std::error_code>> handler_result;

    auto parse_available = [&]() -> task<std::expected<void, std::error_code>>
    {
        while (consumed < wire.readable_bytes())
        {
            const auto available = wire.readable_view().subspan(consumed);
            auto decoded = decode_http3_frame(available);
            if (!decoded)
            {
                if (decoded.error() == std::make_error_code(std::errc::message_size))
                    co_return {};
                co_return std::unexpected(decoded.error());
            }
            if (decoded->second == 0U)
                co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
            if (const auto* frame = std::get_if<headers_frame>(&decoded->first))
            {
                std::expected<std::vector<header_field>, std::error_code> fields;
                {
                    co_await qpack_mutex_.lock();
                    cnetmod::async_lock_guard qpack_guard{qpack_mutex_, std::adopt_lock};
                    auto& completed = completed_headers_[*stream];
                    if (!completed.empty())
                    {
                        fields = std::move(completed.front());
                        completed.pop_front();
                    }
                    else
                        fields = decoder_.decode(frame->encoded_headers, *stream);
                }
                if (!fields)
                {
                    if (fields.error() == std::make_error_code(std::errc::resource_unavailable_try_again))
                        co_return std::unexpected(fields.error());
                    co_return std::unexpected(fields.error());
                }
                const bool trailer = headers_seen;
                if (trailer && trailers_seen)
                    co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
                for (auto& field : *fields)
                {
                    if (!trailer && field.name == ":status")
                    {
                        if (status_seen)
                            co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
                        const auto [end, error] = std::from_chars(field.value.data(),
                            field.value.data() + field.value.size(), response.status);
                        if (error != std::errc{} || end != field.value.data() + field.value.size() ||
                            response.status < 100 || response.status > 999)
                            co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
                        status_seen = true;
                    }
                    else if (!field.name.starts_with(':'))
                        (trailer ? response.trailers : response.headers).insert_or_assign(std::move(field.name), std::move(field.value));
                    else
                        co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
                }
                if (trailer)
                    trailers_seen = true;
                headers_seen = true;
                if (!handler_started)
                {
                    if (!status_seen)
                        co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
                    auto length = response_declared_length(response);
                    if (!length)
                        co_return std::unexpected(length.error());
                    declared_length = *length;
                    handler_started = true;
                    spawn(conn_.context(), handler_task());
                }
            }
            else if (const auto* data = std::get_if<data_frame>(&decoded->first))
            {
                if (!headers_seen || trailers_seen)
                    co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
                const bool no_body = request.method == http_method::HEAD ||
                    (response.status >= 100 && response.status < 200) ||
                    response.status == 204 || response.status == 304;
                if (no_body || (declared_length && body_length + data->data.size() > *declared_length))
                    co_return std::unexpected(std::make_error_code(std::errc::message_size));
                body_length += data->data.size();
                request_body_chunk chunk{data->data.begin(), data->data.end()};
                if (!co_await response.body_stream->send(std::move(chunk)))
                    co_return std::unexpected(cnetmod::make_error_code(cnetmod::errc::operation_aborted));
            }
            else if (const auto* promise = std::get_if<push_promise_frame>(&decoded->first))
            {
                if (!local_max_push_id_ || promise->promised_stream_id > *local_max_push_id_)
                    co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
                {
                    co_await qpack_mutex_.lock();
                    cnetmod::async_lock_guard qpack_guard{qpack_mutex_, std::adopt_lock};
                    if (promised_pushes_.contains(promise->promised_stream_id))
                        co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
                    auto fields = decoder_.decode(promise->encoded_headers, *stream);
                    if (!fields)
                        co_return std::unexpected(fields.error());
                    auto& completed = completed_headers_[*stream];
                    completed.push_back(std::move(*fields));
                    const auto placeholder = encode_http3_frame(headers_frame{{}});
                    auto request = request_from_frames(decoder_,
                        byte_view{placeholder.data(), placeholder.size()}, *stream,
                        completed);
                    if (!request || !request->body.empty() || !request->trailers.empty())
                        co_return std::unexpected(request
                                ? std::make_error_code(std::errc::protocol_error)
                                : request.error());
                    promised_pushes_.emplace(promise->promised_stream_id,
                        std::move(*request));
                }
                (void)push_promise_progress_.try_send({});
                // Run the application decision while no QPACK gate is held:
                // rejecting here sends CANCEL_PUSH before parent DATA and the
                // delayed Push body producer can race into application code.
                co_await dispatch_server_push_promises(*stream);
            }
            else if (std::holds_alternative<goaway_frame>(decoded->first))
                co_return std::unexpected(std::make_error_code(std::errc::connection_aborted));
            else if (!std::holds_alternative<unknown_frame>(decoded->first))
                co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
            consumed += decoded->second;
        }
        if (consumed != 0U)
        {
            wire.consume(consumed);
            consumed = 0U;
        }
        co_return {};
    };

    for (;;)
    {
        auto parsed = co_await parse_available();
        if (!parsed && parsed.error() == std::make_error_code(std::errc::resource_unavailable_try_again))
        {
            auto progress = co_await wait_for_qpack_progress(qpack_progress_, token);
            if (!progress)
            {
                parse_error = progress.error();
                break;
            }
            continue;
        }
        if (!parsed)
        {
            parse_error = parsed.error();
            break;
        }
        if (auto done = handler_done.try_receive(); done)
        {
            handler_result = std::move(*done);
            if (!*handler_result)
                parse_error = handler_result->error();
            if (parse_error)
                break;
        }
        auto received = co_await conn_.async_recv(*stream, wire.prepare(stream_read_chunk_size));
        if (!received)
        {
            if (received.error() == std::make_error_code(std::errc::operation_would_block))
            {
                auto ready = co_await conn_.async_wait_readable(*stream, token);
                if (ready)
                    continue;
                parse_error = ready.error();
                break;
            }
            parse_error = received.error();
            break;
        }
        if (*received == 0U)
        {
            peer_fin = true;
            break;
        }
        wire.commit(*received);
    }

    if (peer_fin && !parse_error)
    {
        auto parsed = co_await parse_available();
        if (!parsed && parsed.error() == std::make_error_code(std::errc::resource_unavailable_try_again))
            parse_error = std::make_error_code(std::errc::protocol_error);
        else if (!parsed)
            parse_error = parsed.error();
        if (wire.readable_bytes() != 0U && !parse_error)
            parse_error = std::make_error_code(std::errc::protocol_error);
        if (!headers_seen || !status_seen)
            parse_error = std::make_error_code(std::errc::protocol_error);
        if (declared_length && body_length != *declared_length)
            parse_error = std::make_error_code(std::errc::message_size);
    }
    response.body_stream->close();
    if (parse_error)
    {
        co_await cancel_stream();
        if (handler_started)
        {
            if (!handler_result)
                handler_result = co_await handler_done.receive();
        }
        co_return std::unexpected(parse_error);
    }
    if (handler_started)
    {
        if (!handler_result)
            handler_result = co_await handler_done.receive();
        if (!handler_result)
            co_return std::unexpected(std::make_error_code(std::errc::not_connected));
        if (!*handler_result)
            co_return std::unexpected(handler_result->error());
    }
    auto decoder_flush = co_await flush_qpack_decoder_instructions(
        conn_, decoder_, qpack_decoder_stream_);
    if (!decoder_flush)
        co_return std::unexpected(decoder_flush.error());
    completed_headers_.erase(*stream);
    (void)conn_.retire_stream(*stream);
    co_return response;
}

auto http3_server_session::process_peer_unidirectional_stream(stream_id id,
    byte_view bytes) -> task<std::expected<void, std::error_code>>
{
    // Peer streams are serviced in independent coroutines. QPACK encoder
    // instructions mutate `decoder_` and therefore serialize only with
    // request HEADERS; decoder instructions mutate `encoder_` and serialize
    // only with response HEADERS. The control stream must not take either
    // gate: a request handler may wait for CANCEL_PUSH while its response is
    // pending, and gating that control frame behind response serialization
    // creates a protocol-level dependency cycle.
    std::uint64_t peer_stream_type{};
    if (const auto known = peer_unidirectional_stream_types_.find(id);
        known != peer_unidirectional_stream_types_.end())
    {
        peer_stream_type = known->second;
    }
    else
    {
        std::size_t type_size{};
        const auto decoded = read_varint(bytes, type_size);
        if (!decoded)
            co_return std::unexpected(decoded.error());
        peer_stream_type = *decoded;
    }

    std::vector<std::uint64_t> cancelled_push_ids;
    const auto validate = [&]
    {
        return validate_peer_uni_stream(conn_, decoder_, encoder_,
            peer_control_stream_seen_, peer_settings_seen_,
            peer_qpack_encoder_stream_seen_, peer_qpack_decoder_stream_seen_,
            peer_datagram_enabled_, peer_connect_protocol_enabled_,
            peer_webtransport_enabled_, peer_max_push_id_, received_goaway_,
            goaway_stream_id_, &cancelled_push_ids, false, id, bytes,
            peer_unidirectional_stream_types_, peer_unidirectional_stream_bytes_);
    };

    std::expected<void, std::error_code> result;
    if (peer_stream_type == qpack_encoder_stream_type)
    {
        co_await request_header_mutex_.lock();
        cnetmod::async_lock_guard guard{request_header_mutex_, std::adopt_lock};
        result = validate();
    }
    else if (peer_stream_type == qpack_decoder_stream_type)
    {
        co_await response_header_mutex_.lock();
        cnetmod::async_lock_guard guard{response_header_mutex_, std::adopt_lock};
        result = validate();
    }
    else
    {
        result = validate();
    }
    if (result)
    {
        co_await apply_push_cancellations(cancelled_push_ids);
        auto completed = decoder_.take_completed_header_blocks();
        const auto completed_count = completed.size();
        for (auto& block : completed)
            completed_headers_[block.stream_id].push_back(std::move(block.headers));
        for (std::size_t index{}; index < completed_count; ++index)
            (void)qpack_progress_.try_send({});
    }
    co_return result;
}

auto http3_client_session::process_peer_unidirectional_stream(stream_id id,
    byte_view bytes) -> task<std::expected<void, std::error_code>>
{
    co_await qpack_mutex_.lock();
    cnetmod::async_lock_guard qpack_guard{qpack_mutex_, std::adopt_lock};
    auto result = validate_peer_uni_stream(conn_, decoder_, encoder_, peer_control_stream_seen_, peer_settings_seen_,
        peer_qpack_encoder_stream_seen_, peer_qpack_decoder_stream_seen_, peer_datagram_enabled_,
        peer_connect_protocol_enabled_, peer_webtransport_enabled_, peer_max_push_id_, received_goaway_,
        goaway_stream_id_, nullptr, true, id, bytes, peer_unidirectional_stream_types_,
        peer_unidirectional_stream_bytes_);
    if (!result)
        co_return result;
    auto completed = decoder_.take_completed_header_blocks();
    const auto completed_count = completed.size();
    for (auto& block : completed)
        completed_headers_[block.stream_id].push_back(std::move(block.headers));
    for (std::size_t index{}; index < completed_count; ++index)
        (void)qpack_progress_.try_send({});
    co_return {};
}

auto http3_client_session::consume_server_push_stream(stream_id id,
    byte_view bytes) -> task<std::expected<void, std::error_code>>
{
    if ((id & 0x03U) != 0x03U || !local_max_push_id_)
        co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
    const auto type = quic::decode_varint(bytes);
    if (!type || type->first != push_stream_type)
        co_return std::unexpected(type
                ? std::make_error_code(std::errc::protocol_error)
                : type.error());
    const auto push_id = quic::decode_varint(bytes.subspan(type->second));
    if (!push_id || push_id->first > *local_max_push_id_)
        co_return std::unexpected(push_id
                ? std::make_error_code(std::errc::protocol_error)
                : push_id.error());
    // QUIC may deliver the unidirectional push stream before the parent
    // request stream's PUSH_PROMISE. Wait for that ordered control point
    // rather than assuming cross-stream receive order.
    bool locally_cancelled{};
    for (;;)
    {
        {
            co_await qpack_mutex_.lock();
            cnetmod::async_lock_guard qpack_guard{qpack_mutex_, std::adopt_lock};
            if (promised_pushes_.contains(push_id->first))
            {
                locally_cancelled = cancelled_pushes_.contains(push_id->first);
                if (locally_cancelled)
                {
                    promised_pushes_.erase(push_id->first);
                    notified_pushes_.erase(push_id->first);
                    cancelled_pushes_.erase(push_id->first);
                }
                break;
            }
        }
        if (conn_.is_closed())
            co_return std::unexpected(std::make_error_code(std::errc::not_connected));
        const auto progressed = co_await push_promise_progress_.receive();
        if (!progressed)
            co_return std::unexpected(std::make_error_code(std::errc::not_connected));
    }

    if (locally_cancelled)
    {
        (void)co_await conn_.async_cancel_stream(id);
        (void)conn_.retire_stream(id);
        co_return {};
    }

    http3_request promise;
    http3_response response;
    for (;;)
    {
        std::expected<http3_response, std::error_code> parsed;
        {
            co_await qpack_mutex_.lock();
            cnetmod::async_lock_guard qpack_guard{qpack_mutex_, std::adopt_lock};
            const auto promised = promised_pushes_.find(push_id->first);
            if (promised == promised_pushes_.end())
                co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
            promise = promised->second;
            auto& completed_headers = completed_headers_[id];
            parsed = response_from_frames(decoder_,
                bytes.subspan(type->second + push_id->second), id,
                completed_headers);
            if (parsed)
            {
                promised_pushes_.erase(push_id->first);
                notified_pushes_.erase(push_id->first);
                cancelled_pushes_.erase(push_id->first);
                completed_headers_.erase(id);
                auto decoder_flush = co_await flush_qpack_decoder_instructions(
                    conn_, decoder_, qpack_decoder_stream_);
                if (!decoder_flush)
                    co_return std::unexpected(decoder_flush.error());
                response = std::move(*parsed);
            }
        }
        if (parsed)
            break;
        if (parsed.error() !=
            std::make_error_code(std::errc::resource_unavailable_try_again))
            co_return std::unexpected(parsed.error());
        // The peer encoder stream shares qpack_mutex_; wait outside the
        // critical section so its instruction consumer can make progress.
        const auto progress = co_await qpack_progress_.receive();
        if (!progress)
            co_return std::unexpected(std::make_error_code(std::errc::not_connected));
    }

    if (push_handler_)
    {
        auto handled = co_await push_handler_(std::move(promise),
            std::move(response));
        if (!handled)
        {
            (void)co_await conn_.async_cancel_stream(id);
            // Application rejection is not an HTTP/3 connection error. The
            // parent request and unrelated pushes remain usable.
            co_return {};
        }
    }
    (void)conn_.retire_stream(id);
    co_return {};
}

auto http3_client_session::accepting_requests() const noexcept -> bool
{
    return !received_goaway_ && !conn_.is_closed();
}

auto http3_client_session::peer_settings_received() const noexcept -> bool
{
    return peer_settings_seen_;
}

auto make_http3_server_session(quic_connection& connection, server_request_handler handler)
    -> std::unique_ptr<http3_server_session>
{
    return std::make_unique<http3_server_session>(connection, std::move(handler));
}

auto http3_client_session::datagrams_enabled() const noexcept -> bool
{
    return settings_.enable_datagram && peer_settings_seen_ && peer_datagram_enabled_;
}

auto http3_client_session::webtransport_enabled() const noexcept -> bool
{
    return datagrams_enabled() && settings_.enable_connect_protocol &&
        settings_.enable_webtransport && peer_connect_protocol_enabled_ &&
        peer_webtransport_enabled_;
}

auto http3_client_session::route_webtransport_stream(stream_id id, byte_view bytes,
    bool unidirectional) -> std::expected<bool, std::error_code>
{
    if (webtransport_sessions_.empty())
        return false;
    if (!unidirectional)
    {
        auto session_id = quic::decode_varint(bytes);
        if (!session_id)
        {
            if (session_id.error() == std::make_error_code(std::errc::bad_message))
                return std::unexpected(std::make_error_code(std::errc::message_size));
            return false;
        }
        if (session_id->first == webtransport_legacy_bidirectional_stream_type)
        {
            session_id = quic::decode_varint(bytes.subspan(session_id->second));
            if (!session_id)
                return std::unexpected(std::make_error_code(std::errc::message_size));
        }
        const auto session = webtransport_sessions_.find(static_cast<stream_id>(session_id->first));
        if (session == webtransport_sessions_.end())
            return false;
        if (!is_webtransport_session_id(session_id->first))
            return std::unexpected(std::make_error_code(std::errc::protocol_error));
        session->second->owned_streams.insert(id);
        if (!session->second->streams.try_send(id))
        {
            session->second->owned_streams.erase(id);
            return std::unexpected(std::make_error_code(std::errc::protocol_error));
        }
        return true;
    }
    const auto type = quic::decode_varint(bytes);
    if (!type)
        return std::unexpected(std::make_error_code(std::errc::message_size));
    if (type->first != webtransport_unidirectional_stream_type)
        return false;
    const auto session_id = quic::decode_varint(bytes.subspan(type->second));
    if (!session_id)
        return std::unexpected(std::make_error_code(std::errc::message_size));
    const auto session = webtransport_sessions_.find(static_cast<stream_id>(session_id->first));
    if (session == webtransport_sessions_.end() || !is_webtransport_session_id(session_id->first))
        return std::unexpected(std::make_error_code(std::errc::protocol_error));
    session->second->owned_streams.insert(id);
    if (!session->second->streams.try_send(id))
    {
        session->second->owned_streams.erase(id);
        return std::unexpected(std::make_error_code(std::errc::protocol_error));
    }
    return true;
}

auto http3_client_session::send_datagram(std::uint64_t context_id, byte_view payload)
    -> task<std::expected<void, std::error_code>>
{
    if (!datagrams_enabled())
        co_return std::unexpected(std::make_error_code(std::errc::not_supported));
    const auto wire = encode_http_datagram({context_id, payload});
    co_return co_await conn_.async_send_datagram(wire);
}

auto http3_client_session::receive_datagram()
    -> task<std::expected<std::pair<std::uint64_t, std::vector<std::byte>>, std::error_code>>
{
    if (!datagrams_enabled())
        co_return std::unexpected(std::make_error_code(std::errc::not_supported));
    if (!datagram_dispatcher_started_)
    {
        datagram_dispatcher_started_ = true;
        spawn(conn_.context(), dispatch_datagrams());
    }
    auto datagram = co_await http_datagrams_.receive();
    if (!datagram)
        co_return std::unexpected(std::make_error_code(std::errc::not_connected));
    co_return std::move(*datagram);
}

auto http3_client_session::dispatch_datagrams() -> task<void>
{
    while (!conn_.is_closed())
    {
        auto wire = co_await conn_.async_receive_datagram();
        if (!wire)
            break;
        auto decoded = decode_http_datagram(byte_view{wire->data(), wire->size()});
        if (!decoded)
            continue;
        std::vector<std::byte> payload(decoded->payload.begin(), decoded->payload.end());
        const auto session_id = webtransport_session_id_from_datagram_context(decoded->context_id);
        if (session_id)
        {
            if (const auto session = webtransport_sessions_.find(*session_id);
                session != webtransport_sessions_.end() && !session->second->closed.load())
            {
                (void)session->second->datagrams.try_send(std::move(payload));
                continue;
            }
        }
        (void)http_datagrams_.try_send(
            std::pair{decoded->context_id, std::move(payload)});
    }
    http_datagrams_.close();
    for (auto& [_, session] : webtransport_sessions_)
        session->datagrams.close();
}

auto make_http3_server_session(quic_connection& connection,
    async_server_request_handler handler) -> std::unique_ptr<http3_server_session>
{
    return std::make_unique<http3_server_session>(connection, std::move(handler));
}

auto make_http3_server_session(quic_connection& connection,
    streaming_server_request_handler handler) -> std::unique_ptr<http3_server_session>
{
    return std::make_unique<http3_server_session>(connection, std::move(handler));
}

auto make_http3_server_session(quic_connection& connection,
    async_webtransport_handler handler) -> std::unique_ptr<http3_server_session>
{
    return std::make_unique<http3_server_session>(connection, std::move(handler));
}

auto make_http3_server_session(quic_connection& connection, http3_server_handlers handlers)
    -> std::unique_ptr<http3_server_session>
{
    return std::make_unique<http3_server_session>(connection, std::move(handlers));
}

auto make_http3_client_session(quic_connection& connection, client_request_handler handler)
    -> std::unique_ptr<http3_client_session>
{
    return std::make_unique<http3_client_session>(connection, std::move(handler));
}

} // namespace cnetmod::http::v3
