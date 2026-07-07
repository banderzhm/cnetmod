/// cnetmod.protocol.coap:client - UDP CoAP client interface.

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.coap:client;

import std;
import :types;
import cnetmod.core.address;
import cnetmod.protocol.udp;
import cnetmod.coro.task;
import cnetmod.io.io_context;

namespace cnetmod::coap {

export class udp_client {
public:
    using observe_handler = std::function<void(const message&)>;

    explicit udp_client(io_context& ctx, client_config cfg = {});

    udp_client(const udp_client&) = delete;
    auto operator=(const udp_client&) -> udp_client& = delete;

    auto request(const endpoint& remote, message req)
        -> task<std::expected<message, std::error_code>>;

    auto get(const endpoint& remote, std::string path, std::string query = {})
        -> task<std::expected<message, std::error_code>>;

    auto get_blockwise(const endpoint& remote, std::string path,
                       std::uint8_t size_exponent = 6)
        -> task<std::expected<message, std::error_code>>;

    auto observe(const endpoint& remote, std::string path, observe_handler handler,
                 std::chrono::milliseconds lifetime = std::chrono::milliseconds{0})
        -> task<std::expected<void, std::error_code>>;

    auto cancel_observe(const endpoint& remote, std::string path)
        -> task<std::expected<message, std::error_code>>;

    auto post(const endpoint& remote, std::string path, std::vector<std::byte> payload,
              content_format format = content_format::octet_stream)
        -> task<std::expected<message, std::error_code>>;

    auto post_blockwise(const endpoint& remote, std::string path, std::vector<std::byte> payload,
                        content_format format = content_format::octet_stream,
                        std::uint8_t size_exponent = 6)
        -> task<std::expected<message, std::error_code>>;

    auto put(const endpoint& remote, std::string path, std::vector<std::byte> payload,
             content_format format = content_format::octet_stream)
        -> task<std::expected<message, std::error_code>>;

    auto put_blockwise(const endpoint& remote, std::string path, std::vector<std::byte> payload,
                       content_format format = content_format::octet_stream,
                       std::uint8_t size_exponent = 6)
        -> task<std::expected<message, std::error_code>>;

    auto delete_(const endpoint& remote, std::string path)
        -> task<std::expected<message, std::error_code>>;

    auto resolve_endpoint(std::string_view host, std::uint16_t port = default_port)
        -> task<std::expected<endpoint, std::error_code>>;

    void close() noexcept;

private:
    auto prepare_request(const endpoint& remote, message req)
        -> std::expected<message, std::error_code>;

    auto receive_matching(const endpoint& remote, const message& req,
                          std::chrono::milliseconds timeout)
        -> task<std::expected<message, std::error_code>>;

    auto receive_notification(const endpoint& remote, const std::vector<std::byte>& token_bytes,
                              std::chrono::milliseconds timeout)
        -> task<std::expected<message, std::error_code>>;

    auto acknowledge(const endpoint& remote, std::uint16_t message_id)
        -> task<std::expected<void, std::error_code>>;

    auto upload_blockwise(const endpoint& remote, method method_code, std::string path,
                          std::vector<std::byte> payload, content_format format,
                          std::uint8_t size_exponent)
        -> task<std::expected<message, std::error_code>>;

    auto initial_ack_timeout() -> std::chrono::milliseconds;
    auto next_message_id() -> std::uint16_t;
    auto next_token() -> std::vector<std::byte>;

    io_context& ctx_;
    udp::udp_socket sock_;
    client_config cfg_;
    std::atomic<std::uint16_t> message_id_{1};
    std::mt19937_64 rng_;
};

} // namespace cnetmod::coap
