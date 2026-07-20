module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.grpc.server;

import std;
import cnetmod.coro.task;
import cnetmod.protocol.http;
import cnetmod.protocol.grpc.types;
import cnetmod.protocol.grpc.codec;
import cnetmod.protocol.grpc.governance.server_policy;

namespace cnetmod::grpc {

namespace {

auto make_context(http::request_context& ctx) -> call_context {
    call_context out;
    out.path = std::string(ctx.path());
    if (auto parsed = parse_service_path(ctx.path())) {
        out.service = std::move(parsed->first);
        out.method = std::move(parsed->second);
    }
    out.headers = metadata_from_headers(ctx.headers());
    out.timeout = timeout_from_headers(ctx.headers());
    out.started = std::chrono::steady_clock::now();
    return out;
}

auto grpc_error(status_code code, std::string message) -> status {
    return status{.code = code, .message = std::move(message)};
}

auto body_to_frames(std::string_view body)
    -> std::expected<std::vector<message_frame>, status>
{
    auto frames = decode_frames(std::span<const std::byte>{
        reinterpret_cast<const std::byte*>(body.data()), body.size()});
    if (!frames) {
        return std::unexpected(grpc_error(status_code::invalid_argument, frames.error().message()));
    }
    return std::move(*frames);
}

auto messages_from_frames(const std::vector<message_frame>& frames,
                          compression_algorithm encoding,
                          const server_options& options)
    -> std::expected<std::vector<byte_buffer>, status>
{
    return frames_to_messages(frames, codec_options{
        .compression = encoding,
        .accept_compressed = options.accept_gzip && encoding == compression_algorithm::gzip,
        .max_message_bytes = options.max_receive_message_bytes,
    });
}

auto validate_request(const http::request_context& ctx, const call_context& call,
                      std::size_t body_size,
                      const server_options& options)
    -> std::expected<void, status>
{
    if (ctx.method() != "POST") {
        return std::unexpected(grpc_error(status_code::invalid_argument, "grpc requires HTTP POST"));
    }
    if (metadata_wire_size(call.headers) > options.max_metadata_bytes) {
        return std::unexpected(grpc_error(status_code::resource_exhausted, "grpc metadata exceeds receive limit"));
    }
    if (body_size > options.max_receive_message_bytes + 5) {
        return std::unexpected(grpc_error(status_code::resource_exhausted, "grpc request body exceeds receive limit"));
    }
    for (const auto& interceptor : options.interceptors) {
        auto accepted = interceptor(call);
        if (!accepted) return std::unexpected(accepted.error());
    }
    return {};
}

auto select_response_compression(const http::request_context& ctx,
                                 const server_options& options) -> compression_algorithm
{
    auto desired = options.default_response_compression;
    if (desired == compression_algorithm::identity) return desired;
    if (accepts_compression(ctx.get_header("grpc-accept-encoding"), desired)) return desired;
    return compression_algorithm::identity;
}

auto validate_outbound(std::span<const byte_buffer> messages, const server_options& options)
    -> std::expected<void, status>
{
    for (const auto& msg : messages) {
        if (msg.size() > options.max_send_message_bytes) {
            return std::unexpected(grpc_error(status_code::resource_exhausted, "grpc response message exceeds send limit"));
        }
    }
    return {};
}

auto status_or_deadline(status st, const call_context& call) -> status {
    if (st.ok() && call.deadline_exceeded()) {
        return grpc_error(status_code::deadline_exceeded, "grpc deadline exceeded");
    }
    return st;
}

auto percent_encode(std::string_view text) -> std::string {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char ch : text) {
        if (ch >= 0x20 && ch < 0x7f && ch != '%') {
            out.push_back(static_cast<char>(ch));
        } else {
            out.push_back('%');
            out.push_back(hex[(ch >> 4) & 0xf]);
            out.push_back(hex[ch & 0xf]);
        }
    }
    return out;
}

auto make_encoded_response(status st, std::span<const std::byte> encoded_frames)
    -> http::response
{
    http::response resp(http::status::ok);
    resp.set_header("Content-Type", "application/grpc");
    resp.set_trailer("grpc-status", std::to_string(static_cast<int>(st.code)));
    if (!st.message.empty()) {
        resp.set_trailer("grpc-message", percent_encode(st.message));
    }
    append_metadata_trailers(resp, st.trailers);
    resp.set_body(std::string(reinterpret_cast<const char*>(encoded_frames.data()), encoded_frames.size()));
    return resp;
}

auto try_unary_identity_payload(std::string_view body)
    -> std::optional<std::span<const std::byte>>
{
    if (body.size() < 5) return std::nullopt;

    const auto* bytes = reinterpret_cast<const std::byte*>(body.data());
    if (std::to_integer<unsigned char>(bytes[0]) != 0) return std::nullopt;

    auto n = (static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[1])) << 24) |
             (static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[2])) << 16) |
             (static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[3])) << 8) |
             static_cast<std::uint32_t>(std::to_integer<unsigned char>(bytes[4]));
    if (static_cast<std::size_t>(n) + 5 != body.size()) return std::nullopt;

    return std::span<const std::byte>{bytes + 5, static_cast<std::size_t>(n)};
}

auto make_unary_identity_response(status st, std::span<const std::byte> payload)
    -> http::response
{
    http::response resp(http::status::ok);
    resp.set_header("Content-Type", "application/grpc");
    resp.set_trailer("grpc-status", std::to_string(static_cast<int>(st.code)));
    if (!st.message.empty()) {
        resp.set_trailer("grpc-message", percent_encode(st.message));
    }
    append_metadata_trailers(resp, st.trailers);

    std::string body;
    if (payload.size() <= std::numeric_limits<std::uint32_t>::max()) {
        body.reserve(payload.size() + 5);
        body.push_back('\0');
        auto n = static_cast<std::uint32_t>(payload.size());
        body.push_back(static_cast<char>((n >> 24) & 0xff));
        body.push_back(static_cast<char>((n >> 16) & 0xff));
        body.push_back(static_cast<char>((n >> 8) & 0xff));
        body.push_back(static_cast<char>(n & 0xff));
        body.append(reinterpret_cast<const char*>(payload.data()), payload.size());
    }
    resp.set_body(std::move(body));
    return resp;
}

} // namespace

auto service_router::key(std::string_view service, std::string_view method) -> std::string {
    return service_path(service, method);
}

void service_router::add_unary(std::string service, std::string method, unary_handler handler) {
    methods_[key(service, method)] = method_entry{
        .kind = call_kind::unary,
        .unary = std::move(handler),
    };
}

service_router::service_router(server_options options)
    : options_(std::move(options))
{
}

void service_router::set_options(server_options options) {
    options_ = std::move(options);
}

auto service_router::options() const noexcept -> const server_options& {
    return options_;
}

void service_router::add_client_streaming(std::string service, std::string method, streaming_handler handler) {
    methods_[key(service, method)] = method_entry{
        .kind = call_kind::client_streaming,
        .streaming = std::move(handler),
    };
}

void service_router::add_server_streaming(std::string service, std::string method, server_streaming_handler handler) {
    methods_[key(service, method)] = method_entry{
        .kind = call_kind::server_streaming,
        .server_streaming = std::move(handler),
    };
}

void service_router::add_bidi_streaming(std::string service, std::string method, streaming_handler handler) {
    methods_[key(service, method)] = method_entry{
        .kind = call_kind::bidi_streaming,
        .streaming = std::move(handler),
    };
}

auto service_router::find(std::string_view path) const -> const method_entry* {
    auto it = methods_.find(std::string(path));
    if (it == methods_.end()) return nullptr;
    return &it->second;
}

auto make_status_response(status st, std::span<const std::byte> payload)
    -> http::response
{
    http::response resp(http::status::ok);
    resp.set_header("Content-Type", "application/grpc");
    resp.set_trailer("grpc-status", std::to_string(static_cast<int>(st.code)));
    if (!st.message.empty()) {
        resp.set_trailer("grpc-message", percent_encode(st.message));
    }
    append_metadata_trailers(resp, st.trailers);
    if (!payload.empty()) {
        auto frame = encode_frame(payload);
        if (frame) {
            resp.set_body(std::string(reinterpret_cast<const char*>(frame->data()), frame->size()));
        }
    } else {
        resp.set_body(std::string{});
    }
    return resp;
}

auto service_router::make_http_handler() const -> http::handler_fn {
    return [this](http::request_context& ctx) -> task<void> {
        auto content_type = ctx.get_header("Content-Type");
        if (!content_type.starts_with("application/grpc")) {
            ctx.resp() = make_status_response(grpc_error(
                status_code::invalid_argument, "content-type must be application/grpc"));
            co_return;
        }

        auto call = make_context(ctx);
        std::optional<governance::call_guard> governance_guard;
        if (options_.governance) {
            auto admitted = options_.governance->begin(call);
            if (!admitted) {
                ctx.resp() = make_status_response(admitted.error());
                co_return;
            }
            governance_guard.emplace(std::move(*admitted));
        }
        const auto complete_governance = [&](const status& result) {
            if (governance_guard) {
                governance_guard->complete(result, call.started);
            }
        };
        auto request_encoding = compression_from_header(ctx.get_header("grpc-encoding"));
        if (!request_encoding) {
            ctx.resp() = make_status_response(grpc_error(
                status_code::unimplemented, "unsupported grpc compression algorithm"));
            co_return;
        }

        const auto* method = find(ctx.path());
        if (!method) {
            ctx.resp() = make_status_response(grpc_error(
                status_code::unimplemented, "grpc method is not registered"));
            co_return;
        }

        auto body = co_await ctx.read_full_body();
        if (auto valid = validate_request(ctx, call, body.size(), options_); !valid) {
            ctx.resp() = make_status_response(valid.error());
            co_return;
        }

        auto response_compression = select_response_compression(ctx, options_);
        if (method->kind == call_kind::unary &&
            *request_encoding == compression_algorithm::identity &&
            response_compression == compression_algorithm::identity) {
            if (auto payload = try_unary_identity_payload(body)) {
                if (call.deadline_exceeded()) {
                    ctx.resp() = make_status_response(grpc_error(
                        status_code::deadline_exceeded, "grpc deadline exceeded"));
                    co_return;
                }

                auto r = co_await method->unary(*payload, call);
                if (!r) {
                    ctx.resp() = make_status_response(status_or_deadline(r.error(), call));
                    co_return;
                }
                if (r->size() > options_.max_send_message_bytes) {
                    ctx.resp() = make_status_response(grpc_error(
                        status_code::resource_exhausted, "grpc response message exceeds send limit"));
                    co_return;
                }

                auto st = status_or_deadline(status{}, call);
                complete_governance(st);
                ctx.resp() = make_unary_identity_response(std::move(st), *r);
                co_return;
            }
        }

        auto frames = body_to_frames(body);
        if (!frames) {
            ctx.resp() = make_status_response(frames.error());
            co_return;
        }
        auto messages = messages_from_frames(*frames, *request_encoding, options_);
        if (!messages) {
            ctx.resp() = make_status_response(messages.error());
            co_return;
        }

        if (call.deadline_exceeded()) {
            ctx.resp() = make_status_response(grpc_error(
                status_code::deadline_exceeded, "grpc deadline exceeded"));
            co_return;
        }

        std::vector<byte_buffer> out_messages;
        status st;

        switch (method->kind) {
        case call_kind::unary: {
            if (messages->size() != 1) {
                st = grpc_error(status_code::invalid_argument, "unary grpc call requires exactly one message");
                break;
            }
            auto r = co_await method->unary(messages->front(), call);
            if (!r) {
                st = r.error();
            } else {
                out_messages.push_back(std::move(*r));
            }
            break;
        }
        case call_kind::client_streaming: {
            auto r = co_await method->streaming(*messages, call);
            if (!r) {
                st = r.error();
            } else {
                out_messages = std::move(*r);
                if (out_messages.size() > 1) {
                    st = grpc_error(status_code::internal, "client-streaming handler returned multiple responses");
                    out_messages.clear();
                }
            }
            break;
        }
        case call_kind::server_streaming: {
            if (messages->size() != 1) {
                st = grpc_error(status_code::invalid_argument, "server-streaming grpc call requires exactly one message");
                break;
            }
            auto r = co_await method->server_streaming(messages->front(), call);
            if (!r) {
                st = r.error();
            } else {
                out_messages = std::move(*r);
            }
            break;
        }
        case call_kind::bidi_streaming: {
            auto r = co_await method->streaming(*messages, call);
            if (!r) {
                st = r.error();
            } else {
                out_messages = std::move(*r);
            }
            break;
        }
        }

        st = status_or_deadline(std::move(st), call);
        complete_governance(st);
        if (auto valid = validate_outbound(out_messages, options_); !valid) {
            ctx.resp() = make_status_response(valid.error());
            co_return;
        }
        auto raw = encode_frames(out_messages, response_compression);
        if (!raw) {
            ctx.resp() = make_status_response(raw.error());
            co_return;
        }
        ctx.resp() = make_encoded_response(std::move(st), *raw);
        if (response_compression != compression_algorithm::identity) {
            ctx.resp().set_header("grpc-encoding", std::string(compression_name(response_compression)));
        }
    };
}

auto make_unary_http_handler(unary_handler handler) -> http::handler_fn {
    return [handler = std::move(handler)](http::request_context& ctx) -> task<void> {
        auto content_type = ctx.get_header("Content-Type");
        if (!content_type.starts_with("application/grpc")) {
            ctx.resp() = make_status_response(grpc_error(
                status_code::invalid_argument, "content-type must be application/grpc"));
            co_return;
        }
        auto call = make_context(ctx);
        auto body = co_await ctx.read_full_body();
        auto frames = body_to_frames(body);
        if (!frames) {
            ctx.resp() = make_status_response(frames.error());
            co_return;
        }
        auto messages = frames_to_messages(*frames);
        if (!messages || messages->size() != 1) {
            ctx.resp() = make_status_response(messages ? grpc_error(
                status_code::invalid_argument, "unary grpc call requires exactly one message") : messages.error());
            co_return;
        }
        auto r = co_await handler(messages->front(), call);
        if (!r) {
            ctx.resp() = make_status_response(r.error());
            co_return;
        }
        ctx.resp() = make_status_response(status{}, *r);
    };
}

} // namespace cnetmod::grpc
