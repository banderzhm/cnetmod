export module cnetmod.protocol.amqp091:delivery_acknowledgement;
import std;
import cnetmod.coro.task;
import :protocol_constants;
import :message_delivery;

export namespace cnetmod::amqp091 {
class protocol_connection;
class logical_channel;

/**
 * @brief Binds acknowledgement to the original connection generation and channel.
 * Operations follow the single-executor contract and do not own the connection.
 */
class delivery_acknowledgement
{
public:
    delivery_acknowledgement() noexcept = default;
    /**
     * @brief Acknowledges this delivery and optionally earlier deliveries.
     */
    [[nodiscard]] auto ack(bool multiple = false) const -> task<result<void>>;
    /**
     * @brief Negatively acknowledges with explicit requeue and range policies.
     */
    [[nodiscard]] auto nack(bool requeue = true, bool multiple = false) const -> task<result<void>>;

private:
    friend class logical_channel;
    delivery_acknowledgement(std::weak_ptr<protocol_connection>, std::uint64_t generation,
        std::uint16_t channel, std::uint64_t tag) noexcept;
    static auto settle(delivery_acknowledgement, std::uint16_t method, std::uint8_t flags)
        -> task<result<void>>;
    std::weak_ptr<protocol_connection> connection_;
    std::uint64_t generation_ = 0;
    std::uint16_t channel_ = 0;
    std::uint64_t tag_ = 0;
};

using acknowledged_delivery_handler = std::function<void(const delivery&, delivery_acknowledgement)>;
} // namespace cnetmod::amqp091
