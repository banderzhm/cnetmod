module cnetmod.protocol.amqp091;
import std;
import :delivery_acknowledgement;
import :protocol_connection;
import :protocol_constants;
import cnetmod.coro.task;

namespace cnetmod::amqp091 {
delivery_acknowledgement::delivery_acknowledgement(std::weak_ptr<protocol_connection> connection,
    std::uint64_t generation, std::uint16_t channel, std::uint64_t tag) noexcept
    : connection_(std::move(connection)), generation_(generation), channel_(channel), tag_(tag) {}

auto delivery_acknowledgement::ack(bool multiple) const -> task<result<void>>
{
    return settle(*this, 80, multiple ? 1 : 0);
}

auto delivery_acknowledgement::nack(bool requeue, bool multiple) const -> task<result<void>>
{
    return settle(*this, 120, (multiple ? 1 : 0) | (requeue ? 2 : 0));
}

auto delivery_acknowledgement::settle(delivery_acknowledgement receipt,
    std::uint16_t method, std::uint8_t flags) -> task<result<void>>
{
    auto connection = receipt.connection_.lock();
    if (!connection)
        co_return std::unexpected(error{.code = error_code::connection_closed});
    co_return co_await connection->async_settle_delivery(receipt.generation_, receipt.channel_,
        receipt.tag_, method, flags);
}
} // namespace cnetmod::amqp091
