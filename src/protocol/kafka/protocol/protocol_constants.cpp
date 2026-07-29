module cnetmod.protocol.kafka.protocol_constants;

import std;

namespace cnetmod::kafka {
auto is_retriable(error_code code) noexcept -> bool
{
    switch (code)
    {
    case error_code::unknown_topic_or_partition:
    case error_code::leader_not_available:
    case error_code::not_leader_or_follower:
    case error_code::request_timed_out:
    case error_code::coordinator_load_in_progress:
    case error_code::coordinator_not_available:
    case error_code::not_coordinator:
    case error_code::duplicate_sequence_number:
    case error_code::concurrent_transactions:
    case error_code::rebalance_in_progress:
    case error_code::transport:
        return true;
    default:
        return false;
    }
}

auto make_error(error_code code, std::string message) -> error
{
    if (message.empty() && code != error_code::none)
        message = std::format(
            "Kafka error code {}", static_cast<std::int16_t>(code));
    return {.code = code,
        .message = std::move(message),
        .retriable = is_retriable(code)};
}
} // namespace cnetmod::kafka
