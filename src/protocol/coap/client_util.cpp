module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.coap;

import :client_util;

import std;
import :client;

namespace cnetmod::coap {

auto udp_client::initial_ack_timeout() -> std::chrono::milliseconds {
    const auto factor = std::max(1.0, cfg_.ack_random_factor);
    const auto scale = std::uniform_real_distribution<double>{1.0, factor}(rng_);
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::duration<double, std::milli>{cfg_.ack_timeout.count() * scale});
}

auto udp_client::next_message_id() -> std::uint16_t {
    return message_id_.fetch_add(1, std::memory_order_relaxed);
}

auto udp_client::next_token() -> std::vector<std::byte> {
    std::vector<std::byte> token(8);
    for (auto& b : token) {
        b = static_cast<std::byte>(std::uniform_int_distribution<int>{0, 255}(rng_));
    }
    return token;
}

} // namespace cnetmod::coap
