module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.grpc.client;

import std;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.protocol.http;
import cnetmod.protocol.grpc.types;

namespace cnetmod::grpc {

export struct client_call {
    std::string service;
    std::string method;
    std::string path;
    metadata headers;
    std::chrono::milliseconds timeout{0};
};

export using client_request_interceptor =
    std::function<std::expected<void, status>(client_call&)>;

export using client_response_interceptor =
    std::function<void(const client_call&, const status&)>;

export struct client_options {
    http::client_options http;
    std::size_t max_receive_message_bytes = default_max_message_bytes;
    std::size_t max_send_message_bytes = default_max_message_bytes;
    std::size_t max_metadata_bytes = default_max_metadata_bytes;
    bool accept_gzip = true;
    compression_algorithm default_compression = compression_algorithm::identity;
    std::vector<client_request_interceptor> request_interceptors;
    std::vector<client_response_interceptor> response_interceptors;
};

export class client {
public:
    explicit client(io_context& ctx, std::string base_url,
                    http::client_options opts = {});
    explicit client(io_context& ctx, std::string base_url,
                    client_options opts);

    [[nodiscard]] auto unary(unary_request req)
        -> task<std::expected<unary_response, status>>;

    [[nodiscard]] auto client_streaming(streaming_request req)
        -> task<std::expected<unary_response, status>>;

    [[nodiscard]] auto server_streaming(unary_request req)
        -> task<std::expected<streaming_response, status>>;

    [[nodiscard]] auto bidi_streaming(streaming_request req)
        -> task<std::expected<streaming_response, status>>;

    void close() noexcept {
        http_.close();
    }

private:
    [[nodiscard]] auto send_streaming(streaming_request req)
        -> task<std::expected<streaming_response, status>>;

    http::client http_;
    std::string base_url_;
    client_options opts_;
};

} // namespace cnetmod::grpc
