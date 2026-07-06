module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.grpc.client;

import std;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.protocol.http;
import cnetmod.protocol.grpc.types;
import cnetmod.protocol.grpc.codec;

namespace cnetmod::grpc {

namespace {

auto make_unavailable(std::string message) -> status {
    return status{.code = status_code::unavailable, .message = std::move(message)};
}

auto make_internal(std::string message) -> status {
    return status{.code = status_code::internal, .message = std::move(message)};
}

auto response_messages(const http::response& resp)
    -> std::expected<std::vector<byte_buffer>, status>
{
    auto encoding = compression_from_header(header_value(resp.headers(), "grpc-encoding"))
        .value_or(compression_algorithm::identity);
    auto body = resp.body();
    auto frames = decode_frames(std::span<const std::byte>{
        reinterpret_cast<const std::byte*>(body.data()), body.size()});
    if (!frames) return std::unexpected(make_internal(frames.error().message()));
    return frames_to_messages(*frames, codec_options{
        .compression = encoding,
        .accept_compressed = encoding != compression_algorithm::identity,
        .max_message_bytes = default_max_message_bytes,
    });
}

auto response_messages(const http::response& resp, const client_options& opts)
    -> std::expected<std::vector<byte_buffer>, status>
{
    auto encoding = compression_from_header(header_value(resp.headers(), "grpc-encoding"))
        .value_or(compression_algorithm::identity);
    auto body = resp.body();
    auto frames = decode_frames(std::span<const std::byte>{
        reinterpret_cast<const std::byte*>(body.data()), body.size()});
    if (!frames) return std::unexpected(make_internal(frames.error().message()));
    return frames_to_messages(*frames, codec_options{
        .compression = encoding,
        .accept_compressed = opts.accept_gzip && encoding == compression_algorithm::gzip,
        .max_message_bytes = opts.max_receive_message_bytes,
    });
}

auto response_unary_identity_payload(const http::response& resp, const client_options& opts)
    -> std::expected<byte_buffer, status>
{
    auto encoding = compression_from_header(header_value(resp.headers(), "grpc-encoding"))
        .value_or(compression_algorithm::identity);
    if (encoding != compression_algorithm::identity) {
        auto messages = response_messages(resp, opts);
        if (!messages) return std::unexpected(messages.error());
        if (messages->size() > 1) {
            return std::unexpected(make_internal("unary grpc response returned multiple messages"));
        }
        if (messages->empty()) return byte_buffer{};
        return std::move(messages->front());
    }

    auto body = resp.body();
    if (body.empty()) return byte_buffer{};
    if (body.size() < 5) {
        return std::unexpected(make_internal("invalid unary grpc frame"));
    }

    const auto* bytes = reinterpret_cast<const std::byte*>(body.data());
    if (std::to_integer<unsigned char>(bytes[0]) != 0) {
        return std::unexpected(make_internal("compressed unary grpc frame missing grpc-encoding"));
    }

    auto n = (static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[1])) << 24) |
             (static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[2])) << 16) |
             (static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[3])) << 8) |
             static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[4]));
    if (static_cast<std::size_t>(n) > opts.max_receive_message_bytes) {
        return std::unexpected(make_status(status_code::resource_exhausted, "grpc message exceeds receive limit"));
    }
    if (static_cast<std::size_t>(n) + 5 != body.size()) {
        return std::unexpected(make_internal("unary grpc response returned multiple messages"));
    }

    byte_buffer out;
    out.assign(bytes + 5, bytes + 5 + n);
    return out;
}

auto make_identity_frame_body(std::span<const std::byte> payload)
    -> std::expected<std::string, status>
{
    if (payload.size() > std::numeric_limits<std::uint32_t>::max()) {
        return std::unexpected(make_status(status_code::resource_exhausted, "grpc message exceeds frame limit"));
    }

    std::string body;
    body.reserve(payload.size() + 5);
    body.push_back('\0');
    auto n = static_cast<std::uint32_t>(payload.size());
    body.push_back(static_cast<char>((n >> 24) & 0xff));
    body.push_back(static_cast<char>((n >> 16) & 0xff));
    body.push_back(static_cast<char>((n >> 8) & 0xff));
    body.push_back(static_cast<char>(n & 0xff));
    body.append(reinterpret_cast<const char*>(payload.data()), payload.size());
    return body;
}

auto validate_outbound(const streaming_request& req, const client_options& opts)
    -> std::expected<void, status>
{
    if (metadata_wire_size(req.headers) > opts.max_metadata_bytes) {
        return std::unexpected(make_status(status_code::resource_exhausted, "grpc metadata exceeds send limit"));
    }
    for (const auto& msg : req.messages) {
        if (msg.size() > opts.max_send_message_bytes) {
            return std::unexpected(make_status(status_code::resource_exhausted, "grpc message exceeds send limit"));
        }
    }
    return {};
}

} // namespace

client::client(io_context& ctx, std::string base_url, http::client_options opts)
    : client(ctx, std::move(base_url), client_options{.http = std::move(opts)})
{
}

client::client(io_context& ctx, std::string base_url, client_options opts)
    : http_(ctx, std::move(opts.http)), base_url_(std::move(base_url)), opts_(std::move(opts))
{
    if (!base_url_.empty() && base_url_.back() == '/') {
        base_url_.pop_back();
    }
}

auto client::send_streaming(streaming_request req)
    -> task<std::expected<streaming_response, status>>
{
    if (req.compression == compression_algorithm::identity) {
        req.compression = opts_.default_compression;
    }
    if (auto valid = validate_outbound(req, opts_); !valid) {
        co_return std::unexpected(valid.error());
    }

    client_call call{
        .service = req.service,
        .method = req.method,
        .path = service_path(req.service, req.method),
        .headers = std::move(req.headers),
        .timeout = req.timeout,
    };
    for (auto& interceptor : opts_.request_interceptors) {
        auto accepted = interceptor(call);
        if (!accepted) co_return std::unexpected(accepted.error());
    }

    auto encoded = encode_frames(req.messages, req.compression);
    if (!encoded) co_return std::unexpected(encoded.error());

    http::request http_req(http::http_method::POST,
        base_url_ + call.path);
    http_req.set_header("Content-Type", "application/grpc");
    http_req.set_header("TE", "trailers");
    http_req.set_header("grpc-accept-encoding", opts_.accept_gzip ? "identity,gzip" : "identity");
    if (req.compression != compression_algorithm::identity) {
        http_req.set_header("grpc-encoding", std::string(compression_name(req.compression)));
    }
    if (req.timeout.count() > 0) {
        http_req.set_header("grpc-timeout", format_timeout(req.timeout));
    }
    append_metadata_headers(http_req, call.headers);
    http_req.set_body(std::string(reinterpret_cast<const char*>(encoded->data()), encoded->size()));

    auto resp = co_await http_.send(http_req);
    if (!resp) co_return std::unexpected(make_unavailable(resp.error().message()));

    streaming_response out;
    out.headers = metadata_from_headers(resp->headers());
    out.st = status_from_response(*resp);
    for (auto& interceptor : opts_.response_interceptors) {
        interceptor(call, out.st);
    }
    auto messages = response_messages(*resp, opts_);
    if (!messages) co_return std::unexpected(messages.error());
    out.messages = std::move(*messages);
    if (!out.st.ok()) co_return std::unexpected(out.st);
    co_return out;
}

auto client::unary(unary_request req)
    -> task<std::expected<unary_response, status>>
{
    if (req.compression == compression_algorithm::identity &&
        opts_.default_compression == compression_algorithm::identity) {
        if (metadata_wire_size(req.headers) > opts_.max_metadata_bytes) {
            co_return std::unexpected(make_status(status_code::resource_exhausted, "grpc metadata exceeds send limit"));
        }
        if (req.payload.size() > opts_.max_send_message_bytes) {
            co_return std::unexpected(make_status(status_code::resource_exhausted, "grpc message exceeds send limit"));
        }

        client_call call{
            .service = req.service,
            .method = req.method,
            .path = service_path(req.service, req.method),
            .headers = std::move(req.headers),
            .timeout = req.timeout,
        };
        for (auto& interceptor : opts_.request_interceptors) {
            auto accepted = interceptor(call);
            if (!accepted) co_return std::unexpected(accepted.error());
        }

        auto body = make_identity_frame_body(req.payload);
        if (!body) co_return std::unexpected(body.error());

        http::request http_req(http::http_method::POST, base_url_ + call.path);
        http_req.set_header("Content-Type", "application/grpc");
        http_req.set_header("TE", "trailers");
        http_req.set_header("grpc-accept-encoding", opts_.accept_gzip ? "identity,gzip" : "identity");
        if (req.timeout.count() > 0) {
            http_req.set_header("grpc-timeout", format_timeout(req.timeout));
        }
        append_metadata_headers(http_req, call.headers);
        http_req.set_body(std::move(*body));

        auto resp = co_await http_.send(http_req);
        if (!resp) co_return std::unexpected(make_unavailable(resp.error().message()));

        unary_response out{
            .st = status_from_response(*resp),
            .headers = metadata_from_headers(resp->headers()),
        };
        for (auto& interceptor : opts_.response_interceptors) {
            interceptor(call, out.st);
        }
        auto payload = response_unary_identity_payload(*resp, opts_);
        if (!payload) co_return std::unexpected(payload.error());
        out.payload = std::move(*payload);
        if (!out.st.ok()) co_return std::unexpected(out.st);
        co_return out;
    }

    streaming_request sr{
        .service = std::move(req.service),
        .method = std::move(req.method),
        .headers = std::move(req.headers),
        .messages = {std::move(req.payload)},
        .timeout = req.timeout,
    };
    auto r = co_await send_streaming(std::move(sr));
    if (!r) co_return std::unexpected(r.error());
    if (r->messages.size() > 1) {
        co_return std::unexpected(make_internal("unary grpc response returned multiple messages"));
    }
    unary_response out{
        .st = std::move(r->st),
        .headers = std::move(r->headers),
    };
    if (!r->messages.empty()) out.payload = std::move(r->messages.front());
    co_return out;
}

auto client::client_streaming(streaming_request req)
    -> task<std::expected<unary_response, status>>
{
    auto r = co_await send_streaming(std::move(req));
    if (!r) co_return std::unexpected(r.error());
    if (r->messages.size() > 1) {
        co_return std::unexpected(make_internal("client-streaming grpc response returned multiple messages"));
    }
    unary_response out{
        .st = std::move(r->st),
        .headers = std::move(r->headers),
    };
    if (!r->messages.empty()) out.payload = std::move(r->messages.front());
    co_return out;
}

auto client::server_streaming(unary_request req)
    -> task<std::expected<streaming_response, status>>
{
    streaming_request sr{
        .service = std::move(req.service),
        .method = std::move(req.method),
        .headers = std::move(req.headers),
        .messages = {std::move(req.payload)},
        .timeout = req.timeout,
    };
    co_return co_await send_streaming(std::move(sr));
}

auto client::bidi_streaming(streaming_request req)
    -> task<std::expected<streaming_response, status>>
{
    co_return co_await send_streaming(std::move(req));
}

} // namespace cnetmod::grpc
