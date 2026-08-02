export module cnetmod.protocol.amqp10:link_state;

export namespace cnetmod::amqp10 {
enum class link_state
{
    detached,
    attach_sent,
    attached,
    detach_sent,
    closed
};
} // namespace cnetmod::amqp10
