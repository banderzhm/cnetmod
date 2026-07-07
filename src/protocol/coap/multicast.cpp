module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.coap;

import :multicast;

import std;
import :codec;
import cnetmod.core.address;
import cnetmod.core.buffer;
import cnetmod.core.error;
import cnetmod.coro.cancel;
import cnetmod.coro.task;
import cnetmod.coro.timer;
import cnetmod.executor.async_op;
import cnetmod.io.io_context;

namespace cnetmod::coap {

auto all_coap_nodes_ipv4(std::uint16_t port) -> endpoint {
    return endpoint{ip_address{ipv4_address{224, 0, 1, 187}}, port};
}

auto all_coap_nodes_ipv6_link_local(std::uint16_t port) -> endpoint {
    auto addr = ipv6_address::from_string("ff02::fd");
    return endpoint{ip_address{addr.value_or(ipv6_address{})}, port};
}

multicast_client::multicast_client(io_context& ctx, multicast_client_config cfg)
    : ctx_(ctx)
    , cfg_(std::move(cfg))
    , rng_(std::random_device{}())
{}

auto multicast_client::request(const endpoint& group, message req)
    -> task<std::expected<std::vector<multicast_response>, std::error_code>>
{
    if (req.code == 0 || !req.is_request()) {
        co_return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }
    req.type = message_type::non_confirmable;
    if (req.message_id == 0) {
        req.message_id = next_message_id();
    }
    if (req.token.empty()) {
        req.token = next_token();
    }

    auto opened = ensure_socket(group.address().family());
    if (!opened) {
        co_return std::unexpected(opened.error());
    }

    auto raw = serialize_message(req);
    if (!raw) {
        co_return std::unexpected(raw.error());
    }
    if (raw->size() > cfg_.coap.max_datagram_size) {
        co_return std::unexpected(std::make_error_code(std::errc::message_size));
    }

    if (auto sent = co_await async_sendto(ctx_, sock_,
            const_buffer{raw->data(), raw->size()}, group); !sent) {
        co_return std::unexpected(sent.error());
    }

    std::vector<multicast_response> responses;
    std::vector<std::byte> buffer(cfg_.coap.max_datagram_size);
    const auto deadline = std::chrono::steady_clock::now() + cfg_.response_timeout;
    std::unordered_set<std::string> seen;

    while (responses.size() < cfg_.max_responses &&
           std::chrono::steady_clock::now() < deadline) {
        endpoint peer;
        cancel_token token;
        auto remaining = deadline - std::chrono::steady_clock::now();
        auto n = co_await with_timeout(ctx_, remaining,
            async_recvfrom(ctx_, sock_, mutable_buffer{buffer.data(), buffer.size()}, peer, token),
            token);
        if (!n) {
            if (n.error() == std::make_error_code(std::errc::operation_canceled) ||
                n.error() == std::make_error_code(std::errc::timed_out) ||
                n.error() == make_error_code(errc::operation_aborted)) {
                break;
            }
            co_return std::unexpected(n.error());
        }

        auto parsed = parse_message(std::span<const std::byte>{buffer.data(), *n});
        if (!parsed || !parsed->is_response() || parsed->token != req.token) {
            continue;
        }

        auto key = std::format("{}#{}#{}", peer.to_string(), parsed->message_id, parsed->code);
        if (!seen.insert(std::move(key)).second) {
            continue;
        }
        responses.push_back(multicast_response{
            .peer = peer,
            .response = std::move(*parsed),
        });
    }

    co_return responses;
}

auto multicast_client::get(const endpoint& group, std::string path, std::string query)
    -> task<std::expected<std::vector<multicast_response>, std::error_code>>
{
    co_return co_await request(group, make_request(request_options{
        .method_code = method::get,
        .path = std::move(path),
        .query = std::move(query),
    }));
}

void multicast_client::close() noexcept {
    sock_.close();
}

auto multicast_client::ensure_socket(address_family family)
    -> std::expected<void, std::error_code>
{
    if (sock_.is_open() && sock_.family() == family) {
        return {};
    }

    sock_.close();
    auto opened = socket::create(family, socket_type::datagram);
    if (!opened) {
        return std::unexpected(opened.error());
    }

    auto opts = socket_options{
        .reuse_address = true,
        .non_blocking = true,
        .ipv6_only = family == address_family::ipv6 ? std::optional<bool>{true} : std::nullopt,
    };
    if (auto applied = opened->apply_options(opts); !applied) {
        return applied;
    }
    if (auto r = opened->set_multicast_hops(family, cfg_.hops); !r) {
        return r;
    }
    if (auto r = opened->set_multicast_loopback(family, cfg_.loopback); !r) {
        return r;
    }

    endpoint local = cfg_.local_endpoint;
    if (local.address().family() != family) {
        local = family == address_family::ipv6
            ? endpoint{ip_address{ipv6_address::any()}, local.port()}
            : endpoint{ip_address{ipv4_address::any()}, local.port()};
    }
    if (auto bound = opened->bind(local); !bound) {
        return bound;
    }

    sock_ = std::move(*opened);
    return {};
}

auto multicast_client::next_message_id() -> std::uint16_t {
    return message_id_.fetch_add(1, std::memory_order_relaxed);
}

auto multicast_client::next_token() -> std::vector<std::byte> {
    std::vector<std::byte> token(8);
    for (auto& b : token) {
        b = static_cast<std::byte>(std::uniform_int_distribution<int>{0, 255}(rng_));
    }
    return token;
}

} // namespace cnetmod::coap
