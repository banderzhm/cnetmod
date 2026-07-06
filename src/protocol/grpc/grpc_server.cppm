module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.grpc.server;

import std;
import cnetmod.coro.task;
import cnetmod.protocol.http;
import cnetmod.protocol.grpc.types;

namespace cnetmod::grpc {

export using unary_handler =
    std::function<task<std::expected<byte_buffer, status>>(
        std::span<const std::byte>, const call_context&)>;

export using streaming_handler =
    std::function<task<std::expected<std::vector<byte_buffer>, status>>(
        std::span<const byte_buffer>, const call_context&)>;

export using server_streaming_handler =
    std::function<task<std::expected<std::vector<byte_buffer>, status>>(
        std::span<const std::byte>, const call_context&)>;

export using server_interceptor =
    std::function<std::expected<void, status>(const call_context&)>;

export struct server_options {
    std::size_t max_receive_message_bytes = default_max_message_bytes;
    std::size_t max_send_message_bytes = default_max_message_bytes;
    std::size_t max_metadata_bytes = default_max_metadata_bytes;
    bool accept_gzip = true;
    compression_algorithm default_response_compression = compression_algorithm::identity;
    std::vector<server_interceptor> interceptors;
};

export class service_router {
public:
    service_router() = default;
    explicit service_router(server_options options);

    void set_options(server_options options);
    [[nodiscard]] auto options() const noexcept -> const server_options&;

    void add_unary(std::string service, std::string method, unary_handler handler);
    void add_client_streaming(std::string service, std::string method, streaming_handler handler);
    void add_server_streaming(std::string service, std::string method, server_streaming_handler handler);
    void add_bidi_streaming(std::string service, std::string method, streaming_handler handler);

    [[nodiscard]] auto make_http_handler() const -> http::handler_fn;

private:
    struct method_entry {
        call_kind kind = call_kind::unary;
        unary_handler unary;
        streaming_handler streaming;
        server_streaming_handler server_streaming;
    };

    [[nodiscard]] auto find(std::string_view path) const -> const method_entry*;
    [[nodiscard]] static auto key(std::string_view service, std::string_view method) -> std::string;

    std::map<std::string, method_entry> methods_;
    server_options options_;
};

export [[nodiscard]] auto make_status_response(status st, std::span<const std::byte> payload = {})
    -> http::response;

export [[nodiscard]] auto make_unary_http_handler(unary_handler handler)
    -> http::handler_fn;

} // namespace cnetmod::grpc
