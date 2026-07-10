module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.coap;

import :client_observe;

import std;
import :types;
import :codec;
import :client;
import cnetmod.core.address;
import cnetmod.core.buffer;
import cnetmod.coro.cancel;
import cnetmod.coro.task;
import cnetmod.coro.timer;
import cnetmod.executor.async_op;

namespace cnetmod::coap {
namespace {

auto observe_is_newer(std::uint32_t incoming, std::uint32_t previous) noexcept -> bool {
    incoming &= 0x00ffffffu;
    previous &= 0x00ffffffu;
    if (incoming == previous) {
        return false;
    }
    return incoming > previous
        ? incoming - previous < 0x800000u
        : previous - incoming > 0x800000u;
}

auto observe_sequence(const message& msg) -> std::optional<std::uint32_t> {
    auto opt = msg.first_option(option_number::observe);
    if (!opt) {
        return std::nullopt;
    }
    auto value = option_uint_value(opt->value);
    if (!value) {
        return std::nullopt;
    }
    return *value & 0x00ffffffu;
}

} // namespace

auto udp_client::observe(const endpoint& remote, std::string path, observe_handler handler,
                         std::chrono::milliseconds lifetime)
    -> task<std::expected<void, std::error_code>>
{
    if (!handler) {
        co_return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }

    auto req = make_request(request_options{
        .method_code = method::get,
        .path = std::move(path),
        .observe = 0,
    });

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

    auto sent = co_await async_sendto(ctx_, sock_.native_socket(),
        const_buffer{raw->data(), raw->size()}, remote);
    if (!sent) {
        co_return std::unexpected(sent.error());
    }

    auto deadline = lifetime.count() > 0
        ? std::chrono::steady_clock::now() + lifetime
        : std::chrono::steady_clock::time_point::max();
    std::size_t delivered = 0;
    std::optional<std::uint32_t> last_sequence;
    while (std::chrono::steady_clock::now() < deadline) {
        const auto wait = lifetime.count() > 0
            ? std::chrono::duration_cast<std::chrono::milliseconds>(
                deadline - std::chrono::steady_clock::now())
            : cfg_.exchange_lifetime;
        auto notification = co_await receive_notification(remote, req.token, wait);
        if (!notification) {
            if (notification.error() == std::make_error_code(std::errc::timed_out) &&
                std::chrono::steady_clock::now() < deadline) {
                auto resent = co_await async_sendto(ctx_, sock_.native_socket(),
                    const_buffer{raw->data(), raw->size()}, remote);
                if (!resent) {
                    co_return std::unexpected(resent.error());
                }
                continue;
            }
            co_return std::unexpected(notification.error());
        }
        if (auto seq = observe_sequence(*notification)) {
            if (last_sequence && !observe_is_newer(*seq, *last_sequence)) {
                continue;
            }
            last_sequence = *seq;
        }
        handler(*notification);
        ++delivered;
        if (cfg_.max_observe_notifications != 0 &&
            delivered >= cfg_.max_observe_notifications) {
            co_return {};
        }
    }

    co_return {};
}

auto udp_client::cancel_observe(const endpoint& remote, std::string path)
    -> task<std::expected<message, std::error_code>>
{
    co_return co_await request(remote, make_request(request_options{
        .method_code = method::get,
        .path = std::move(path),
        .observe = 1,
    }));
}

auto udp_client::receive_notification(const endpoint& remote,
                                      const std::vector<std::byte>& token_bytes,
                                      std::chrono::milliseconds timeout)
    -> task<std::expected<message, std::error_code>>
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    std::vector<std::byte> buffer(cfg_.max_datagram_size);

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
        if (!parsed || parsed->token != token_bytes || !parsed->is_response()) {
            continue;
        }
        if (parsed->type == message_type::confirmable) {
            (void)co_await acknowledge(remote, parsed->message_id);
        }
        co_return std::move(*parsed);
    }

    co_return std::unexpected(std::make_error_code(std::errc::timed_out));
}

auto udp_client::acknowledge(const endpoint& remote, std::uint16_t message_id)
    -> task<std::expected<void, std::error_code>>
{
    message ack;
    ack.type = message_type::acknowledgement;
    ack.message_id = message_id;
    auto raw = serialize_message(ack);
    if (!raw) {
        co_return std::unexpected(raw.error());
    }
    auto sent = co_await async_sendto(ctx_, sock_.native_socket(),
        const_buffer{raw->data(), raw->size()}, remote);
    if (!sent) {
        co_return std::unexpected(sent.error());
    }
    co_return {};
}

} // namespace cnetmod::coap
