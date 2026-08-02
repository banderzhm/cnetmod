export module cnetmod.protocol.amqp10:session_state;

export namespace cnetmod::amqp10 {
enum class session_state
{
    unmapped,
    begin_sent,
    mapped,
    end_sent,
    ended
};
} // namespace cnetmod::amqp10
