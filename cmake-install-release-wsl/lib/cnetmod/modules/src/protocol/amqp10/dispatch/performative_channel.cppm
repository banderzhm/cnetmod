module;
#include <cnetmod/config.hpp>
export module cnetmod.protocol.amqp10:performative_channel;
import std;
import :client_configuration;
import :client_error;
import :reconnect_policy;
import cnetmod.coro.task;
import cnetmod.coro.cancel;
import :performative_model;
import :recovery_observer;

export namespace cnetmod::amqp10 {
class performative_channel
{
public:
    virtual ~performative_channel() = default;
    virtual auto send(std::uint16_t, const performative&, cancel_token&)
        -> task<std::expected<void, error>> = 0;
    virtual auto receive(std::uint16_t, cancel_token&)
        -> task<std::expected<performative, error>> = 0;
    [[nodiscard]] virtual auto maximum_frame_size() const noexcept
        -> std::uint32_t = 0;
    virtual void register_recovery_observer(recovery_observer&) = 0;
    virtual void unregister_recovery_observer(recovery_observer&) noexcept = 0;
};
} // namespace cnetmod::amqp10
