module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.amqp091;

import :reconnect_policy;
import std;

namespace cnetmod::amqp091 {

exponential_backoff::exponential_backoff(std::chrono::milliseconds initial,
    std::chrono::milliseconds maximum,
    double multiplier,
    std::size_t maximum_attempts) noexcept
    : initial_(std::max(initial, std::chrono::milliseconds{0})),
      maximum_(std::max(maximum, initial_)),
      multiplier_(std::max(multiplier, 1.0)),
      maximum_attempts_(maximum_attempts) {}

auto exponential_backoff::next_delay(const reconnect_context& context) const
    -> std::optional<std::chrono::milliseconds>
{
    if (maximum_attempts_ != 0 && context.attempt >= maximum_attempts_)
        return std::nullopt;

    auto delay = static_cast<long double>(initial_.count());
    for (std::size_t index = 0; index < context.attempt; ++index)
    {
        delay *= multiplier_;
        if (delay >= static_cast<long double>(maximum_.count()))
            return maximum_;
    }
    return std::min(std::chrono::milliseconds{static_cast<std::int64_t>(delay)},
        maximum_);
}

} // namespace cnetmod::amqp091
