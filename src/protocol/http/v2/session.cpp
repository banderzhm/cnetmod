module cnetmod.protocol.http.v2.session;

import std;
import cnetmod.core.buffer;
import cnetmod.executor.async_op;

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

auto read_u32(std::span<const std::byte> value) -> std::uint32_t {
    return (std::to_integer<std::uint32_t>(value[0]) << 24) |
           (std::to_integer<std::uint32_t>(value[1]) << 16) |
           (std::to_integer<std::uint32_t>(value[2]) << 8) |
            std::to_integer<std::uint32_t>(value[3]);
}
}

session::session(cnetmod::io_context& context, cnetmod::socket& socket,
                 server_handler handler, transport_reader reader,
                 transport_writer writer)
    : context_(&context), socket_(&socket), reader_(std::move(reader)),
      writer_(std::move(writer)), handler_(std::move(handler))
{
    if (!reader_) {
        reader_ = [this](cnetmod::mutable_buffer buffer)
            -> cnetmod::task<std::expected<std::size_t, std::error_code>> {
            co_return co_await cnetmod::async_read(*context_, *socket_, buffer);
        };
    }
    if (!writer_) {
        writer_ = [this](cnetmod::const_buffer buffer)
            -> cnetmod::task<std::expected<void, std::error_code>> {
            co_return co_await cnetmod::async_write_all(*context_, *socket_, buffer);
        };
    }
}

auto session::peer_settings() const noexcept -> const settings& { return peer_; }
auto session::local_settings() noexcept -> settings& { return local_; }
auto session::take_outbound() -> std::vector<std::byte> { return std::exchange(outbound_, {}); }

void session::queue_frame(frame_header header, std::span<const std::byte> payload) {
    header.length = static_cast<std::uint32_t>(payload.size());
    const auto encoded = encode_frame_header(header);
    outbound_.insert(outbound_.end(), encoded.begin(), encoded.end());
    outbound_.insert(outbound_.end(), payload.begin(), payload.end());
}

void session::queue_connection_error(std::uint32_t code) {
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

auto session::validate_headers(std::span<const header_field> fields) const -> std::error_code {
    bool regular_seen = false;
    bool method = false;
    bool scheme = false;
    bool path = false;
    for (const auto& field : fields) {
        if (field.name.empty()) return std::make_error_code(std::errc::protocol_error);
        for (const auto ch : field.name)
            if (ch >= 'A' && ch <= 'Z') return std::make_error_code(std::errc::protocol_error);
        if (field.name.starts_with(':')) {
            if (regular_seen) return std::make_error_code(std::errc::protocol_error);
            method |= field.name == ":method";
            scheme |= field.name == ":scheme";
            path |= field.name == ":path";
        } else {
            regular_seen = true;
            if (field.name == "connection" || field.name == "keep-alive" ||
                field.name == "proxy-connection" || field.name == "transfer-encoding" ||
                field.name == "upgrade")
                return std::make_error_code(std::errc::protocol_error);
        }
    }
    return method && scheme && path ? std::error_code{} : std::make_error_code(std::errc::protocol_error);
}

auto session::dispatch_ready() -> cnetmod::task<void> {
    auto ready = std::exchange(ready_streams_, {});
    for (const auto stream_id : ready) {
        const auto found = streams_.find(stream_id);
        if (found == streams_.end()) continue;
        server_response response;
        if (handler_) {
            server_request request{.stream_id = stream_id,
                                   .headers = found->second.headers(),
                                   .body = {found->second.body().begin(), found->second.body().end()}};
            response = co_await handler_(std::move(request));
        }
        std::vector<header_field> headers;
        headers.reserve(response.headers.size() + 2);
        headers.push_back({":status", std::to_string(response.status)});
        bool has_length = false;
        for (auto& header : response.headers) {
            if (header.name.starts_with(':') || header.name == "connection" ||
                header.name == "transfer-encoding" || header.name == "upgrade") continue;
            has_length |= header.name == "content-length";
            headers.push_back(std::move(header));
        }
        if (!response.body.empty() && !has_length)
            headers.push_back({"content-length", std::to_string(response.body.size())});
        auto encoded = encoder_.encode(headers);
        if (!encoded) co_return;
        const auto end_on_headers = response.body.empty();
        queue_frame({.type = frame_type::headers,
                     .flags = static_cast<std::uint8_t>(flag_end_headers | (end_on_headers ? flag_end_stream : 0)),
                     .stream_id = stream_id}, *encoded);
        std::size_t offset = 0;
        while (offset < response.body.size()) {
            const auto size = std::min<std::size_t>(peer_.max_frame_size, response.body.size() - offset);
            const auto final = offset + size == response.body.size();
            queue_frame({.type = frame_type::data,
                         .flags = static_cast<std::uint8_t>(final ? flag_end_stream : 0),
                         .stream_id = stream_id},
                        {response.body.data() + offset, size});
            offset += size;
        }
    }
}

auto session::receive(std::span<const std::byte> bytes) -> std::expected<void, std::error_code> {
    input_.insert(input_.end(), bytes.begin(), bytes.end());
    if (!received_preface_) {
        if (input_.size() < client_preface.size()) return {};
        if (std::string_view(reinterpret_cast<const char*>(input_.data()), client_preface.size()) != client_preface)
            return std::unexpected(std::make_error_code(std::errc::protocol_error));
        input_.erase(input_.begin(), input_.begin() + static_cast<std::ptrdiff_t>(client_preface.size()));
        received_preface_ = true;
        queue_frame({.type = frame_type::settings, .stream_id = 0}, encode_settings(local_));
    }
    while (input_.size() >= frame_header_size) {
        auto header = decode_frame_header(std::span{input_.data(), frame_header_size});
        if (!header) return std::unexpected(header.error());
        if (input_.size() < frame_header_size + header->length) return {};
        auto payload = std::span{input_.data() + frame_header_size, static_cast<std::size_t>(header->length)};
        auto result = process_frame(*header, payload);
        if (!result) {
            queue_connection_error(result.error() == std::make_error_code(std::errc::message_size)
                                       ? error_frame_size : error_protocol);
            return result;
        }
        input_.erase(input_.begin(), input_.begin() + static_cast<std::ptrdiff_t>(frame_header_size + header->length));
    }
    return {};
}

auto session::process_frame(frame_header header, std::span<const std::byte> payload)
    -> std::expected<void, std::error_code>
{
    if (header.length > local_.max_frame_size)
        return std::unexpected(std::make_error_code(std::errc::message_size));
    if (continuation_stream_id_ != 0 && header.type != frame_type::continuation)
        return std::unexpected(std::make_error_code(std::errc::protocol_error));
    if (header.type == frame_type::settings) {
        if (header.stream_id != 0) return std::unexpected(std::make_error_code(std::errc::protocol_error));
        if ((header.flags & flag_ack) != 0) return payload.empty() ? std::expected<void, std::error_code>{} : std::unexpected(std::make_error_code(std::errc::protocol_error));
        if (auto error = decode_settings(payload, peer_); error) return std::unexpected(error);
        // Peer SETTINGS_HEADER_TABLE_SIZE constrains this endpoint's encoder;
        // decoder capacity is controlled by the table-size updates in HPACK blocks.
        encoder_.set_dynamic_table_limit(peer_.header_table_size);
        received_settings_ = true;
        queue_frame({.type = frame_type::settings, .flags = flag_ack, .stream_id = 0}, {});
        return {};
    }
    if (header.type == frame_type::headers || header.type == frame_type::continuation) {
        if (header.stream_id == 0)
            return std::unexpected(std::make_error_code(std::errc::protocol_error));
        if (header.type == frame_type::continuation && header.stream_id != continuation_stream_id_)
            return std::unexpected(std::make_error_code(std::errc::protocol_error));
        if (header.type == frame_type::headers && continuation_stream_id_ != 0)
            return std::unexpected(std::make_error_code(std::errc::protocol_error));
        std::size_t offset = 0;
        if (header.type == frame_type::headers && (header.flags & flag_padded)) {
            if (payload.empty()) return std::unexpected(std::make_error_code(std::errc::protocol_error));
            offset = 1 + std::to_integer<std::uint8_t>(payload[0]);
        }
        if (header.type == frame_type::headers && (header.flags & flag_priority)) offset += 5;
        if (offset > payload.size()) return std::unexpected(std::make_error_code(std::errc::protocol_error));
        header_block_.insert(header_block_.end(), payload.begin() + static_cast<std::ptrdiff_t>(offset), payload.end());
        if ((header.flags & flag_end_headers) == 0) {
            continuation_stream_id_ = header.stream_id;
            continuation_end_stream_ = (header.flags & flag_end_stream) != 0;
            return {};
        }
        continuation_stream_id_ = 0;
        auto decoded = decoder_.decode(header_block_);
        header_block_.clear();
        if (!decoded) return std::unexpected(decoded.error());
        if (auto error = validate_headers(*decoded); error) return std::unexpected(error);
        if ((header.stream_id & 1U) == 0 || header.stream_id <= last_peer_stream_id_)
            return std::unexpected(std::make_error_code(std::errc::protocol_error));
        last_peer_stream_id_ = header.stream_id;
        auto [it, inserted] = streams_.try_emplace(header.stream_id, header.stream_id, static_cast<std::int32_t>(peer_.initial_window_size));
        const bool end_stream = continuation_end_stream_ || (header.flags & flag_end_stream) != 0;
        continuation_end_stream_ = false;
        if (!inserted || !it->second.receive_headers(std::move(*decoded), end_stream))
            return std::unexpected(std::make_error_code(std::errc::protocol_error));
        if (end_stream) {
            ready_streams_.push_back(header.stream_id);
        }
        return {};
    }
    if (header.type == frame_type::data) {
        auto it = streams_.find(header.stream_id);
        if (header.stream_id == 0 || payload.size() > static_cast<std::size_t>(std::max(receive_window_, 0)) ||
            it == streams_.end() || !it->second.receive_data(payload, (header.flags & flag_end_stream) != 0))
            return std::unexpected(std::make_error_code(std::errc::protocol_error));
        receive_window_ -= static_cast<std::int32_t>(payload.size());
        if (receive_window_ <= 32'767) {
            const auto increment = static_cast<std::uint32_t>(65'535 - receive_window_);
            receive_window_ += static_cast<std::int32_t>(increment);
            std::array<std::byte, 4> update{static_cast<std::byte>(increment >> 24), static_cast<std::byte>(increment >> 16), static_cast<std::byte>(increment >> 8), static_cast<std::byte>(increment)};
            queue_frame({.type = frame_type::window_update}, update);
        }
        if ((header.flags & flag_end_stream) != 0) {
            ready_streams_.push_back(header.stream_id);
        }
        return {};
    }
    if (header.type == frame_type::ping) {
        if (header.stream_id != 0 || payload.size() != 8) return std::unexpected(std::make_error_code(std::errc::protocol_error));
        if ((header.flags & flag_ack) == 0) queue_frame({.type = frame_type::ping, .flags = flag_ack}, payload);
    }
    if (header.type == frame_type::window_update) {
        if (payload.size() != 4) return std::unexpected(std::make_error_code(std::errc::protocol_error));
        const auto increment = read_u32(payload) & 0x7fff'ffffU;
        if (increment == 0 || increment > static_cast<std::uint32_t>(std::numeric_limits<std::int32_t>::max() - send_window_))
            return std::unexpected(std::make_error_code(std::errc::protocol_error));
        send_window_ += static_cast<std::int32_t>(increment);
        return {};
    }
    if (header.type == frame_type::rst_stream) {
        if (header.stream_id == 0 || payload.size() != 4) return std::unexpected(std::make_error_code(std::errc::protocol_error));
        streams_.erase(header.stream_id);
        return {};
    }
    if (header.type == frame_type::goaway) {
        if (header.stream_id != 0 || payload.size() < 8) return std::unexpected(std::make_error_code(std::errc::protocol_error));
        goaway_received_ = true;
        return {};
    }
    return {};
}

auto session::run(std::span<const std::byte> initial) -> cnetmod::task<void> {
    if (!initial.empty()) {
        if (auto result = receive(initial); !result) {
            auto output = take_outbound();
            if (!output.empty()) (void)co_await writer_({output.data(), output.size()});
            co_return;
        }
        co_await dispatch_ready();
        auto output = take_outbound();
        if (!output.empty()) {
            auto written = co_await writer_({output.data(), output.size()});
            if (!written) co_return;
        }
    }
    std::array<std::byte, 16 * 1024> buffer{};
    while (true) {
        auto read = co_await reader_({buffer.data(), buffer.size()});
        if (!read || *read == 0) co_return;
        if (auto result = receive(std::span{buffer.data(), *read}); !result) {
            auto output = take_outbound();
            if (!output.empty()) (void)co_await writer_({output.data(), output.size()});
            co_return;
        }
        co_await dispatch_ready();
        auto output = take_outbound();
        if (!output.empty()) {
            auto written = co_await writer_({output.data(), output.size()});
            if (!written) co_return;
        }
    }
}

} // namespace cnetmod::http::v2
