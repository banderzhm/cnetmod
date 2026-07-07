module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.coap;

import :server_lifecycle;

import std;
import :types;
import :codec;
import :server;
import cnetmod.core.address;
import cnetmod.core.buffer;
import cnetmod.core.socket;
import cnetmod.executor.async_op;

namespace cnetmod::coap {

udp_server::udp_server(io_context& ctx, server_config cfg)
    : ctx_(ctx), cfg_(cfg) {}

auto udp_server::listen(std::string_view host, std::uint16_t port, socket_options opts)
    -> std::expected<void, std::error_code>
{
    auto addr = ip_address::from_string(host);
    if (!addr) {
        return std::unexpected(addr.error());
    }

    auto sock_result = socket::create(
        addr->is_v4() ? address_family::ipv4 : address_family::ipv6,
        socket_type::datagram);
    if (!sock_result) {
        return std::unexpected(sock_result.error());
    }

    sock_ = std::move(*sock_result);
    opts.reuse_address = true;
    opts.non_blocking = true;
    if (auto applied = sock_.apply_options(opts); !applied) {
        return std::unexpected(applied.error());
    }
    return sock_.bind(endpoint{*addr, port});
}

void udp_server::set_handler(request_handler handler) {
    handler_ = std::move(handler);
}

void udp_server::set_etag_provider(etag_provider provider) {
    etag_provider_ = std::move(provider);
}

void udp_server::route(method m, std::string path, request_handler handler) {
    auto normalized = normalize_path(path);
    router_.add(m, std::move(path), std::move(handler));
    if (m == method::get && normalized != "/.well-known/core") {
        register_resource(resource_description{.path = std::move(normalized)});
    }
    handler_ = [this](const inbound_request& req, const endpoint& peer) {
        return router_.dispatch(req, peer);
    };
}

void udp_server::register_resource(resource_description desc) {
    desc.path = normalize_path(std::move(desc.path));
    std::scoped_lock lock(resources_mutex_);
    auto it = std::ranges::find_if(resources_, [&](const auto& item) {
        return item.path == desc.path;
    });
    if (it == resources_.end()) {
        resources_.push_back(std::move(desc));
    } else {
        *it = std::move(desc);
    }
}

auto udp_server::join_multicast_group(const ip_address& group,
                                      std::optional<ip_address> local_address,
                                      unsigned int interface_index)
    -> std::expected<void, std::error_code>
{
    return sock_.join_multicast_group(group, std::move(local_address), interface_index);
}

auto udp_server::leave_multicast_group(const ip_address& group,
                                       std::optional<ip_address> local_address,
                                       unsigned int interface_index)
    -> std::expected<void, std::error_code>
{
    return sock_.leave_multicast_group(group, std::move(local_address), interface_index);
}

auto udp_server::run() -> task<void> {
    running_ = true;
    std::vector<std::byte> buffer(cfg_.max_datagram_size);

    while (running_ && sock_.is_open()) {
        endpoint peer;
        auto n = co_await async_recvfrom(ctx_, sock_,
            mutable_buffer{buffer.data(), buffer.size()}, peer);
        if (!n || *n == 0) {
            continue;
        }

        auto parsed = parse_message(std::span<const std::byte>{buffer.data(), *n});
        if (!parsed) {
            continue;
        }
        if (!parsed->is_request()) {
            if (handle_observe_control(peer, *parsed)) {
                continue;
            }
            if (parsed->type == message_type::confirmable) {
                (void)co_await send_reset(peer, parsed->message_id);
            }
            continue;
        }

        auto path = extract_path(*parsed);
        auto query = extract_query(*parsed);
        inbound_request req{
            .request = std::move(*parsed),
            .path = std::move(path),
            .query = std::move(query),
        };

        if (has_unsupported_critical_option(req.request)) {
            auto bad = make_response(req.request, response_code::bad_option);
            normalize_response(req.request, bad);
            auto raw = serialize_message(bad);
            if (raw) {
                (void)co_await async_sendto(ctx_, sock_,
                    const_buffer{raw->data(), raw->size()}, peer);
            }
            continue;
        }

        if (auto precondition = check_preconditions(req)) {
            normalize_response(req.request, *precondition);
            auto raw = serialize_message(*precondition);
            if (raw) {
                (void)co_await async_sendto(ctx_, sock_,
                    const_buffer{raw->data(), raw->size()}, peer);
            }
            continue;
        }

        auto block1_state = apply_block1(peer, req);
        if (block1_state == block1_result::continue_) {
            auto cont = make_response(req.request, response_code::continue_);
            auto block = req.block1().value();
            cont.add_option(option_number::block1, encode_block_option(block));
            auto raw = serialize_message(cont);
            if (raw) {
                (void)co_await async_sendto(ctx_, sock_,
                    const_buffer{raw->data(), raw->size()}, peer);
            }
            continue;
        }
        if (block1_state == block1_result::incomplete ||
            block1_state == block1_result::too_large) {
            auto err = make_response(req.request,
                block1_state == block1_result::incomplete
                    ? response_code::request_entity_incomplete
                    : response_code::request_entity_too_large);
            normalize_response(req.request, err);
            auto raw = serialize_message(err);
            if (raw) {
                (void)co_await async_sendto(ctx_, sock_,
                    const_buffer{raw->data(), raw->size()}, peer);
            }
            continue;
        }

        auto proxied = co_await proxy_request(req);
        auto discovery = proxied ? std::optional<message>{std::move(*proxied)}
                                 : make_discovery_response(req);
        auto resp = discovery
            ? std::move(*discovery)
            : (handler_
                ? co_await handler_(req, peer)
                : make_response(req.request, response_code::not_implemented));

        if (!request_accepts_response(req, resp)) {
            resp = make_response(req.request, response_code::not_acceptable);
        }
        apply_observe_state(req, peer, resp);
        normalize_response(req.request, resp);
        apply_block2(req, resp);

        auto raw = serialize_message(resp);
        if (!raw || raw->size() > cfg_.max_datagram_size) {
            auto err = make_response(req.request, response_code::internal_server_error);
            raw = serialize_message(err);
            if (!raw) {
                continue;
            }
        }

        (void)co_await async_sendto(ctx_, sock_,
            const_buffer{raw->data(), raw->size()}, peer);
    }
}

void udp_server::stop() noexcept {
    running_ = false;
    sock_.close();
}

auto udp_server::is_running() const noexcept -> bool {
    return running_;
}

} // namespace cnetmod::coap
