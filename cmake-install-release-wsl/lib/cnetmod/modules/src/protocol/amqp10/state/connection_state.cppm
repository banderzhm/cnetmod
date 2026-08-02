export module cnetmod.protocol.amqp10:connection_state;

export namespace cnetmod::amqp10 {
enum class connection_state
{
    idle,
    connecting,
    sasl,
    opening,
    opened,
    closing,
    closed,
    failed
};
} // namespace cnetmod::amqp10
