module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.coap;

import :server_utils;

import std;
import :types;
import :codec;
import :server;
import cnetmod.core.address;
import cnetmod.core.buffer;
import cnetmod.coro.task;
import cnetmod.executor.async_op;

namespace cnetmod::coap {

auto udp_server::block1_key::operator==(const block1_key& rhs) const noexcept -> bool {
    return peer == rhs.peer && token == rhs.token && path == rhs.path;
}

auto udp_server::block1_hash::operator()(const block1_key& key) const noexcept -> std::size_t {
    auto h = std::hash<std::string>{}(key.peer);
    h ^= std::hash<std::string>{}(key.path) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    for (auto b : key.token) {
        h ^= std::hash<unsigned>{}(std::to_integer<unsigned>(b)) +
             0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    }
    return h;
}

auto udp_server::observer_key::operator==(const observer_key& rhs) const noexcept -> bool {
    return peer == rhs.peer && token == rhs.token && path == rhs.path;
}

auto udp_server::observer_hash::operator()(const observer_key& key) const noexcept -> std::size_t {
    auto h = std::hash<std::string>{}(key.peer);
    h ^= std::hash<std::string>{}(key.path) + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    for (auto b : key.token) {
        h ^= std::hash<unsigned>{}(std::to_integer<unsigned>(b)) +
             0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2);
    }
    return h;
}

auto udp_server::normalize_path(std::string path) -> std::string {
    if (path.empty()) {
        return "/";
    }
    if (path.front() != '/') {
        path.insert(path.begin(), '/');
    }
    while (path.size() > 1 && path.back() == '/') {
        path.pop_back();
    }
    return path;
}

void udp_server::normalize_response(const message& req, message& resp) {
    if (resp.code == 0) {
        resp.set_response(response_code::content);
    }
    if (resp.token.empty()) {
        resp.token = req.token;
    }
    if (resp.message_id == 0 || req.type == message_type::confirmable) {
        resp.message_id = req.message_id;
    }
    if (req.type == message_type::confirmable) {
        resp.type = message_type::acknowledgement;
    } else if (resp.type == message_type::confirmable) {
        resp.type = message_type::non_confirmable;
    }
}

auto udp_server::is_known_option(std::uint16_t number) noexcept -> bool {
    switch (static_cast<option_number>(number)) {
        case option_number::if_match:
        case option_number::uri_host:
        case option_number::etag:
        case option_number::if_none_match:
        case option_number::observe:
        case option_number::uri_port:
        case option_number::location_path:
        case option_number::uri_path:
        case option_number::content_format:
        case option_number::max_age:
        case option_number::uri_query:
        case option_number::accept:
        case option_number::location_query:
        case option_number::block2:
        case option_number::block1:
        case option_number::size2:
        case option_number::proxy_uri:
        case option_number::proxy_scheme:
        case option_number::size1:
            return true;
    }
    return false;
}

auto udp_server::has_unsupported_critical_option(const message& msg) noexcept -> bool {
    return std::ranges::any_of(msg.options, [](const option& opt) {
        return (opt.number & 0x01u) != 0 && !is_known_option(opt.number);
    });
}

auto udp_server::response_content_format(const message& resp) -> std::optional<std::uint32_t> {
    auto opt = resp.first_option(option_number::content_format);
    if (!opt) {
        return std::nullopt;
    }
    auto value = option_uint_value(opt->value);
    if (!value) {
        return std::nullopt;
    }
    return *value;
}

auto udp_server::request_accepts_response(const inbound_request& req, const message& resp) -> bool {
    if ((resp.code >> 5) != 2 || resp.payload.empty()) {
        return true;
    }
    auto format = response_content_format(resp);
    if (!format) {
        return true;
    }

    auto accepts = req.request.find_options(option_number::accept);
    if (accepts.empty()) {
        return true;
    }
    return std::ranges::any_of(accepts, [&](const option& opt) {
        auto accepted = option_uint_value(opt.value);
        return accepted && *accepted == *format;
    });
}

auto udp_server::check_preconditions(const inbound_request& req) const -> std::optional<message> {
    if (!etag_provider_) {
        return std::nullopt;
    }

    const auto current = etag_provider_(req.path);
    const auto if_match = req.request.find_options(option_number::if_match);
    if (!if_match.empty()) {
        const bool matched = current.has_value() && std::ranges::any_of(if_match, [&](const option& opt) {
            return opt.value.empty() || opt.value == *current;
        });
        if (!matched) {
            return make_response(req.request, response_code::precondition_failed);
        }
    }

    const auto if_none_match = req.request.find_options(option_number::if_none_match);
    if (!if_none_match.empty() && current.has_value()) {
        return make_response(req.request, response_code::precondition_failed);
    }

    return std::nullopt;
}

auto udp_server::next_message_id() -> std::uint16_t {
    return message_id_.fetch_add(1, std::memory_order_relaxed);
}

auto udp_server::next_observe_sequence() -> std::uint32_t {
    return observe_sequence_.fetch_add(1, std::memory_order_relaxed) & 0x00ffffffu;
}

auto udp_server::send_reset(const endpoint& peer, std::uint16_t message_id)
    -> task<std::expected<void, std::error_code>>
{
    message rst;
    rst.type = message_type::reset;
    rst.message_id = message_id;
    auto raw = serialize_message(rst);
    if (!raw) {
        co_return std::unexpected(raw.error());
    }
    auto sent = co_await async_sendto(ctx_, sock_,
        const_buffer{raw->data(), raw->size()}, peer);
    if (!sent) {
        co_return std::unexpected(sent.error());
    }
    co_return {};
}

} // namespace cnetmod::coap
