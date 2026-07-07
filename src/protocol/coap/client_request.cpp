module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.coap;

import :client_request;

import std;
import :types;
import :codec;
import :client;
import cnetmod.core.address;
import cnetmod.core.buffer;
import cnetmod.core.socket;
import cnetmod.coro.cancel;
import cnetmod.coro.timer;
import cnetmod.executor.async_op;

namespace cnetmod::coap {

auto udp_client::request(const endpoint& remote, message req)
    -> task<std::expected<message, std::error_code>>
{
    auto prepared = prepare_request(remote, std::move(req));
    if (!prepared) {
        co_return std::unexpected(prepared.error());
    }
    req = std::move(*prepared);

    auto raw = serialize_message(req);
    if (!raw) {
        co_return std::unexpected(raw.error());
    }
    if (raw->size() > cfg_.max_datagram_size) {
        co_return std::unexpected(std::make_error_code(std::errc::message_size));
    }

    const auto attempts = req.type == message_type::confirmable
        ? static_cast<std::uint8_t>(cfg_.max_retransmit + 1)
        : static_cast<std::uint8_t>(1);

    auto timeout = initial_ack_timeout();
    for (std::uint8_t attempt = 0; attempt < attempts; ++attempt) {
        auto sent = co_await async_sendto(ctx_, sock_.native_socket(),
            const_buffer{raw->data(), raw->size()}, remote);
        if (!sent) {
            co_return std::unexpected(sent.error());
        }

        auto received = co_await receive_matching(remote, req, timeout);
        if (received) {
            co_return std::move(*received);
        }
        if (received.error() != std::make_error_code(std::errc::timed_out) &&
            received.error() != std::make_error_code(std::errc::operation_canceled)) {
            co_return std::unexpected(received.error());
        }
        timeout *= 2;
    }

    co_return std::unexpected(std::make_error_code(std::errc::timed_out));
}

auto udp_client::prepare_request(const endpoint& remote, message req)
    -> std::expected<message, std::error_code>
{
    if (!sock_.is_open()) {
        auto opened = sock_.open(remote.address().is_v4()
            ? address_family::ipv4 : address_family::ipv6);
        if (!opened) {
            return std::unexpected(opened.error());
        }
    }
    if (req.code == 0 || !req.is_request()) {
        return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }
    if (req.message_id == 0) {
        req.message_id = next_message_id();
    }
    if (req.token.empty()) {
        req.token = next_token();
    }
    return req;
}

auto udp_client::receive_matching(const endpoint& remote, const message& req,
                                  std::chrono::milliseconds timeout)
    -> task<std::expected<message, std::error_code>>
{
    auto deadline = std::chrono::steady_clock::now() + timeout;
    std::vector<std::byte> buffer(cfg_.max_datagram_size);
    bool empty_ack_seen = false;

    while (std::chrono::steady_clock::now() < deadline) {
        endpoint peer;
        cancel_token token;
        const auto remaining = deadline - std::chrono::steady_clock::now();
        auto n = co_await with_timeout(ctx_, remaining,
            async_recvfrom(ctx_, sock_.native_socket(),
                mutable_buffer{buffer.data(), buffer.size()}, peer, token),
            token);
        if (!n) {
            if (n.error() == std::make_error_code(std::errc::operation_canceled)) {
                co_return std::unexpected(std::make_error_code(std::errc::timed_out));
            }
            co_return std::unexpected(n.error());
        }

        if (peer.to_string() != remote.to_string()) {
            continue;
        }

        auto parsed = parse_message(std::span<const std::byte>{buffer.data(), *n});
        if (!parsed) {
            continue;
        }
        if (parsed->type == message_type::reset && parsed->message_id == req.message_id) {
            co_return std::unexpected(std::make_error_code(std::errc::connection_aborted));
        }
        if (parsed->type == message_type::acknowledgement &&
            parsed->code == 0 &&
            parsed->message_id == req.message_id &&
            !empty_ack_seen) {
            empty_ack_seen = true;
            deadline = std::chrono::steady_clock::now() + cfg_.exchange_lifetime;
            continue;
        }
        if (parsed->token == req.token &&
            (parsed->message_id == req.message_id || parsed->type != message_type::acknowledgement) &&
            parsed->is_response()) {
            co_return std::move(*parsed);
        }
    }

    co_return std::unexpected(std::make_error_code(std::errc::timed_out));
}

} // namespace cnetmod::coap
