module;
#include <cnetmod/config.hpp>
export module cnetmod.protocol.amqp10:transaction_controller;
import std;
import :client_configuration;
import :client_error;
import :reconnect_policy;
import cnetmod.coro.task;
import cnetmod.coro.cancel;
import :primitive_value;

export namespace cnetmod::amqp10 {
class performative_channel;

class transaction_controller
{
public:
    ~transaction_controller();
    transaction_controller(transaction_controller&&) noexcept;
    auto operator=(transaction_controller&&) noexcept
        -> transaction_controller&;
    transaction_controller(const transaction_controller&) = delete;
    auto operator=(const transaction_controller&)
        -> transaction_controller& = delete;
    auto declare(cancel_token&) -> task<std::expected<binary, error>>;
    auto discharge(std::span<const std::byte> transaction_id, bool fail,
        cancel_token&) -> task<std::expected<void, error>>;

private:
    friend class session;
    struct impl;
    explicit transaction_controller(std::unique_ptr<impl>);
    static auto create(performative_channel&, std::uint16_t, std::uint32_t)
        -> transaction_controller;
    std::unique_ptr<impl> impl_;
};
} // namespace cnetmod::amqp10
