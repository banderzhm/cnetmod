/// cnetmod.protocol.coap:multicast - RFC 7252 multicast CoAP client support.

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.coap:multicast;

import std;
import :types;
import cnetmod.core.address;
import cnetmod.core.socket;
import cnetmod.coro.task;
import cnetmod.io.io_context;

namespace cnetmod::coap {

export struct multicast_response {
    endpoint peer;
    message response;
};

export struct multicast_client_config {
    client_config coap;
    endpoint local_endpoint{ip_address{ipv4_address::any()}, 0};
    bool loopback = true;
    int hops = 1;
    std::size_t max_responses = 16;
    std::chrono::milliseconds response_timeout{1500};
};

export [[nodiscard]] auto all_coap_nodes_ipv4(std::uint16_t port = default_port) -> endpoint;
export [[nodiscard]] auto all_coap_nodes_ipv6_link_local(std::uint16_t port = default_port) -> endpoint;

export class multicast_client {
public:
    explicit multicast_client(io_context& ctx, multicast_client_config cfg = {});

    multicast_client(const multicast_client&) = delete;
    auto operator=(const multicast_client&) -> multicast_client& = delete;

    auto request(const endpoint& group, message req)
        -> task<std::expected<std::vector<multicast_response>, std::error_code>>;

    auto get(const endpoint& group, std::string path, std::string query = {})
        -> task<std::expected<std::vector<multicast_response>, std::error_code>>;

    void close() noexcept;

private:
    auto ensure_socket(address_family family) -> std::expected<void, std::error_code>;
    auto next_message_id() -> std::uint16_t;
    auto next_token() -> std::vector<std::byte>;

    io_context& ctx_;
    multicast_client_config cfg_;
    socket sock_;
    std::atomic<std::uint16_t> message_id_{1};
    std::mt19937_64 rng_;
};

} // namespace cnetmod::coap
