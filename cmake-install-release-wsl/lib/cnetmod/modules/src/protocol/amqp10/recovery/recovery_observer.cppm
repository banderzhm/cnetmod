module;
#include <cnetmod/config.hpp>
export module cnetmod.protocol.amqp10:recovery_observer;
import std;
import :client_configuration;
import :client_error;
import :reconnect_policy;
import cnetmod.coro.task;
import cnetmod.coro.cancel;

export namespace cnetmod::amqp10 {
class recovery_observer
{
public:
    virtual ~recovery_observer() = default;
    [[nodiscard]] virtual auto recovery_order() const noexcept
        -> std::uint8_t = 0;
    virtual auto recover(cancel_token&)
        -> task<std::expected<void, error>> = 0;
};
} // namespace cnetmod::amqp10
