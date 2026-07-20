module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.coap;

import :server_observe;

import std;
import :types;
import :codec;
import :server;
import cnetmod.core.address;
import cnetmod.core.buffer;
import cnetmod.coro.task;
import cnetmod.executor.async_op;

namespace cnetmod::coap {

auto udp_server::notify_observers(std::string path, message notification)
    -> task<std::expected<std::size_t, std::error_code>>
{
    auto subscribers = collect_observers(normalize_path(std::move(path)));
    std::size_t delivered = 0;
    std::error_code last_error;

    for (const auto& sub : subscribers) {
        auto msg = notification;
        msg.type = cfg_.observe_confirmable_notifications
            ? message_type::confirmable
            : message_type::non_confirmable;
        msg.message_id = next_message_id();
        msg.token = sub.token;
        msg.add_uint_option(option_number::observe, next_observe_sequence());
        if (!msg.first_option(option_number::max_age)) {
            msg.add_uint_option(option_number::max_age,
                static_cast<std::uint32_t>(cfg_.observe_max_age.count()));
        }

        auto raw = serialize_message(msg);
        if (!raw || raw->size() > cfg_.max_datagram_size) {
            last_error = raw ? std::make_error_code(std::errc::message_size) : raw.error();
            continue;
        }
        auto sent = co_await async_sendto(ctx_, sock_,
            const_buffer{raw->data(), raw->size()}, sub.peer);
        if (!sent) {
            last_error = sent.error();
            remove_observer(sub.peer, sub.token, sub.path);
            continue;
        }
        if (msg.type == message_type::confirmable) {
            track_observe_delivery(msg.message_id, sub);
        }
        ++delivered;
    }

    if (delivered == 0 && last_error) {
        co_return std::unexpected(last_error);
    }
    co_return delivered;
}

void udp_server::apply_observe_state(const inbound_request& req, const endpoint& peer, message& resp) {
    if (!cfg_.enable_observe) {
        return;
    }

    const auto status_class = resp.code >> 5;
    if (req.is_observe_register() && status_class == 2) {
        add_observer(peer, req.request.token, req.path);
        resp.add_uint_option(option_number::observe, next_observe_sequence());
        if (!resp.first_option(option_number::max_age)) {
            resp.add_uint_option(option_number::max_age,
                static_cast<std::uint32_t>(cfg_.observe_max_age.count()));
        }
        return;
    }

    if (req.is_observe_deregister()) {
        remove_observer(peer, req.request.token, req.path);
    }
}

void udp_server::add_observer(const endpoint& peer, const std::vector<std::byte>& token, std::string path) {
    auto normalized = normalize_path(std::move(path));
    observer_key key{
        .peer = peer.to_string(),
        .token = token,
        .path = normalized,
    };
    observe_subscription sub{
        .peer = peer,
        .token = token,
        .path = std::move(normalized),
        .updated_at = std::chrono::steady_clock::now(),
    };
    std::scoped_lock lock(observers_mutex_);
    observers_[std::move(key)] = std::move(sub);
}

void udp_server::remove_observer(const endpoint& peer, const std::vector<std::byte>& token, std::string path) {
    observer_key key{
        .peer = peer.to_string(),
        .token = token,
        .path = normalize_path(std::move(path)),
    };
    std::scoped_lock lock(observers_mutex_);
    observers_.erase(key);
}

auto udp_server::collect_observers(std::string path) -> std::vector<observe_subscription> {
    std::vector<observe_subscription> result;
    const auto now = std::chrono::steady_clock::now();
    std::scoped_lock lock(observers_mutex_);
    for (auto it = observers_.begin(); it != observers_.end();) {
        if (now - it->second.updated_at > cfg_.observe_max_age * 3) {
            it = observers_.erase(it);
            continue;
        }
        if (it->second.path == path) {
            result.push_back(it->second);
        }
        ++it;
    }
    return result;
}

void udp_server::track_observe_delivery(std::uint16_t message_id,
                                        const observe_subscription& sub)
{
    observe_delivery delivery{
        .peer = sub.peer,
        .token = sub.token,
        .path = sub.path,
        .sent_at = std::chrono::steady_clock::now(),
    };
    std::scoped_lock lock(observers_mutex_);
    pending_observe_[observe_delivery_key(sub.peer, message_id)] = std::move(delivery);
}

auto udp_server::handle_observe_control(const endpoint& peer, const message& msg) -> bool {
    if (msg.type != message_type::acknowledgement && msg.type != message_type::reset) {
        return false;
    }

    std::optional<observe_delivery> delivery;
    {
        std::scoped_lock lock(observers_mutex_);
        auto it = pending_observe_.find(observe_delivery_key(peer, msg.message_id));
        if (it == pending_observe_.end()) {
            return false;
        }
        delivery = std::move(it->second);
        pending_observe_.erase(it);
    }

    if (msg.type == message_type::reset) {
        remove_observer(delivery->peer, delivery->token, delivery->path);
    }
    return true;
}

auto udp_server::observe_delivery_key(const endpoint& peer,
                                      std::uint16_t message_id) -> std::string
{
    return std::format("{}#{}", peer.to_string(), message_id);
}

} // namespace cnetmod::coap
