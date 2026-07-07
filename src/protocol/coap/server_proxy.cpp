module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.coap;

import :server_proxy;

import std;
import :types;
import :codec;
import :server;
import cnetmod.core.address;
import cnetmod.core.buffer;
import cnetmod.core.dns;
import cnetmod.core.socket;
import cnetmod.coro.cancel;
import cnetmod.coro.timer;
import cnetmod.executor.async_op;

namespace cnetmod::coap {

auto udp_server::parse_proxy_uri(std::string_view uri) -> std::optional<proxy_target> {
    static constexpr std::string_view scheme = "coap://";
    if (!uri.starts_with(scheme)) {
        return std::nullopt;
    }
    uri.remove_prefix(scheme.size());
    const auto slash = uri.find('/');
    const auto authority = uri.substr(0, slash);
    const auto path_query = slash == std::string_view::npos ? std::string_view{"/"} : uri.substr(slash);

    std::string_view host;
    std::uint16_t port = default_port;
    if (authority.starts_with('[')) {
        const auto close = authority.find(']');
        if (close == std::string_view::npos) {
            return std::nullopt;
        }
        host = authority.substr(1, close - 1);
        if (close + 1 < authority.size()) {
            if (authority[close + 1] != ':') {
                return std::nullopt;
            }
            auto port_text = authority.substr(close + 2);
            auto parsed_port = std::from_chars(port_text.data(), port_text.data() + port_text.size(), port);
            if (parsed_port.ec != std::errc{}) {
                return std::nullopt;
            }
        }
    } else {
        const auto colon = authority.rfind(':');
        if (colon == std::string_view::npos) {
            host = authority;
        } else {
            host = authority.substr(0, colon);
            auto port_text = authority.substr(colon + 1);
            auto parsed_port = std::from_chars(port_text.data(), port_text.data() + port_text.size(), port);
            if (parsed_port.ec != std::errc{}) {
                return std::nullopt;
            }
        }
    }
    if (host.empty()) {
        return std::nullopt;
    }

    auto query_pos = path_query.find('?');
    return proxy_target{
        .host = std::string{host},
        .port = port,
        .path = std::string(path_query.substr(0, query_pos)),
        .query = query_pos == std::string_view::npos
            ? std::string{}
            : std::string(path_query.substr(query_pos + 1)),
    };
}

auto udp_server::proxy_request(const inbound_request& req) -> task<std::optional<message>> {
    if (!cfg_.enable_proxy) {
        co_return std::nullopt;
    }
    auto proxy = req.request.first_option(option_number::proxy_uri);
    if (!proxy) {
        co_return std::nullopt;
    }
    auto target = parse_proxy_uri(proxy->as_string());
    if (!target) {
        co_return make_response(req.request, response_code::proxying_not_supported);
    }

    endpoint remote;
    if (auto addr = ip_address::from_string(target->host)) {
        remote = endpoint{*addr, target->port};
    } else {
        auto addrs = co_await async_resolve_addresses(ctx_, target->host,
            std::to_string(target->port));
        if (!addrs || addrs->empty()) {
            co_return make_response(req.request, response_code::bad_gateway);
        }
        remote = endpoint{addrs->front(), target->port};
    }

    message out = req.request;
    std::erase_if(out.options, [](const option& opt) {
        return opt.number == static_cast<std::uint16_t>(option_number::proxy_uri) ||
               opt.number == static_cast<std::uint16_t>(option_number::proxy_scheme) ||
               opt.number == static_cast<std::uint16_t>(option_number::uri_host) ||
               opt.number == static_cast<std::uint16_t>(option_number::uri_port) ||
               opt.number == static_cast<std::uint16_t>(option_number::uri_path) ||
               opt.number == static_cast<std::uint16_t>(option_number::uri_query);
    });
    for (auto part_start = std::size_t{0}; part_start < target->path.size();) {
        while (part_start < target->path.size() && target->path[part_start] == '/') {
            ++part_start;
        }
        const auto end = target->path.find('/', part_start);
        const auto count = end == std::string::npos ? target->path.size() - part_start : end - part_start;
        if (count > 0) {
            out.add_string_option(option_number::uri_path,
                std::string_view{target->path}.substr(part_start, count));
        }
        if (end == std::string::npos) {
            break;
        }
        part_start = end + 1;
    }
    if (!target->query.empty()) {
        out.add_string_option(option_number::uri_query, target->query);
    }

    auto upstream = socket::create(
        remote.address().is_v4() ? address_family::ipv4 : address_family::ipv6,
        socket_type::datagram);
    if (!upstream) {
        co_return make_response(req.request, response_code::bad_gateway);
    }
    if (auto applied = upstream->apply_options(socket_options{.non_blocking = true}); !applied) {
        co_return make_response(req.request, response_code::bad_gateway);
    }

    auto raw = serialize_message(out);
    if (!raw) {
        co_return make_response(req.request, response_code::bad_request);
    }
    auto sent = co_await async_sendto(ctx_, *upstream,
        const_buffer{raw->data(), raw->size()}, remote);
    if (!sent) {
        co_return make_response(req.request, response_code::bad_gateway);
    }

    std::vector<std::byte> buffer(cfg_.max_datagram_size);
    endpoint peer;
    cancel_token token;
    auto received = co_await with_timeout(ctx_, std::chrono::seconds(5),
        async_recvfrom(ctx_, *upstream, mutable_buffer{buffer.data(), buffer.size()}, peer, token),
        token);
    if (!received || peer.to_string() != remote.to_string()) {
        co_return make_response(req.request, response_code::gateway_timeout);
    }
    auto parsed = parse_message(std::span<const std::byte>{buffer.data(), *received});
    if (!parsed) {
        co_return make_response(req.request, response_code::bad_gateway);
    }
    parsed->message_id = req.request.message_id;
    parsed->token = req.request.token;
    co_return std::move(*parsed);
}

} // namespace cnetmod::coap
