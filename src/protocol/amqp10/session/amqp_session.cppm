module;
#include <cnetmod/config.hpp>
export module cnetmod.protocol.amqp10:amqp_session;
import std;
import :client_configuration;
import :client_error;
import :reconnect_policy;
import cnetmod.coro.task;
import cnetmod.coro.cancel;
import :session_state;
import :delivery_state;
import :sender_link;
import :receiver_link;
import :transaction_controller;

export namespace cnetmod::amqp10 {
class performative_channel;

struct session_options
{
    std::uint32_t incoming_window = 2048;
    std::uint32_t outgoing_window = 2048;
    std::uint32_t handle_max = 65535;
};

struct sender_options
{
    std::string name;
    target target_terminus;
    sender_settle_mode sender_settlement = sender_settle_mode::mixed;
    receiver_settle_mode receiver_settlement = receiver_settle_mode::first;
};

struct receiver_options
{
    std::string name;
    source source_terminus;
    sender_settle_mode sender_settlement = sender_settle_mode::mixed;
    receiver_settle_mode receiver_settlement = receiver_settle_mode::first;
};

class session
{
public:
    ~session();
    session(session&&) noexcept;
    auto operator=(session&&) noexcept -> session&;
    session(const session&) = delete;
    auto operator=(const session&) -> session& = delete;
    auto begin(cancel_token&) -> task<std::expected<void, error>>;
    [[nodiscard]] auto make_sender(sender_options)
        -> std::expected<sender_link, error>;
    [[nodiscard]] auto make_receiver(receiver_options)
        -> std::expected<receiver_link, error>;
    [[nodiscard]] auto make_transaction_controller()
        -> std::expected<transaction_controller, error>;
    auto end(cancel_token&) -> task<std::expected<void, error>>;
    [[nodiscard]] auto state() const noexcept -> session_state;
    [[nodiscard]] auto channel() const noexcept -> std::uint16_t;

private:
    friend class client;
    struct impl;
    explicit session(std::unique_ptr<impl>);
    static auto create(performative_channel&, std::uint16_t, session_options)
        -> session;
    std::unique_ptr<impl> impl_;
};
} // namespace cnetmod::amqp10
