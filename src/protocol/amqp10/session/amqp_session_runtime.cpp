module;
#include <cnetmod/config.hpp>
module cnetmod.protocol.amqp10;
import :amqp_session;
import std;
import :performative_channel;
import :recovery_observer;
import :performative_model;
import :protocol_error;

namespace cnetmod::amqp10 {
struct session::impl : recovery_observer
{
    performative_channel* owner;
    std::uint16_t channel_id;
    session_options options;
    session_state current = session_state::unmapped;
    std::uint32_t next_outgoing_id = 1;
    std::uint32_t next_handle = 0;

    ~impl()
    {
        if (owner)
            owner->unregister_recovery_observer(*this);
    }

    auto recovery_order() const noexcept -> std::uint8_t override
    {
        return 1;
    }

    auto recover(cancel_token& token)
        -> task<std::expected<void, error>> override
    {
        if (current == session_state::unmapped || current == session_state::ended)
            co_return {};
        current = session_state::unmapped;
        cnetmod::amqp10::begin request{.next_outgoing_id = next_outgoing_id,
            .incoming_window = options.incoming_window,
            .outgoing_window = options.outgoing_window,
            .handle_max = options.handle_max};
        auto sent = co_await owner->send(channel_id, performative{request}, token);
        if (!sent)
            co_return std::unexpected(sent.error());
        current = session_state::begin_sent;
        auto peer = co_await owner->receive(channel_id, token);
        if (!peer || !std::holds_alternative<amqp10::begin>(*peer))
            co_return std::unexpected(
                peer ? make_error(error_stage::protocol,
                           errc::unexpected_performative,
                           "expected peer Begin during recovery")
                     : peer.error());
        current = session_state::mapped;
        co_return {};
    }
};

session::session(std::unique_ptr<impl> p)
    : impl_(std::move(p)) {}

session::~session() = default;
session::session(session&&) noexcept = default;
auto session::operator=(session&&) noexcept -> session& = default;

auto session::create(performative_channel& owner, std::uint16_t channel,
    session_options options) -> session
{
    auto state = std::make_unique<impl>();
    state->owner = &owner;
    state->channel_id = channel;
    state->options = options;
    owner.register_recovery_observer(*state);
    return session(std::move(state));
}

auto session::begin(cancel_token& token)
    -> task<std::expected<void, error>>
{
    if (impl_->current != session_state::unmapped)
        co_return std::unexpected(make_error(error_stage::protocol,
            errc::protocol_state,
            "session is already begun"));
    cnetmod::amqp10::begin request{
        .next_outgoing_id = impl_->next_outgoing_id,
        .incoming_window = impl_->options.incoming_window,
        .outgoing_window = impl_->options.outgoing_window,
        .handle_max = impl_->options.handle_max};
    auto sent = co_await impl_->owner->send(impl_->channel_id,
        performative{request}, token);
    if (!sent)
        co_return std::unexpected(sent.error());
    impl_->current = session_state::begin_sent;
    auto peer = co_await impl_->owner->receive(impl_->channel_id, token);
    if (!peer || !std::holds_alternative<amqp10::begin>(*peer))
        co_return std::unexpected(
            peer ? make_error(error_stage::protocol,
                       errc::unexpected_performative, "expected peer Begin")
                 : peer.error());
    impl_->current = session_state::mapped;
    co_return {};
}

auto session::make_sender(sender_options options)
    -> std::expected<sender_link, error>
{
    if (impl_->current != session_state::mapped)
        return std::unexpected(make_error(error_stage::protocol,
            errc::protocol_state,
            "session is not mapped"));
    if (impl_->next_handle > impl_->options.handle_max)
        return std::unexpected(make_error(error_stage::flow_control,
            errc::protocol_state,
            "session handle maximum reached"));
    return sender_link::create(
        *impl_->owner, impl_->channel_id, impl_->next_handle++,
        std::move(options.name), std::move(options.target_terminus),
        options.sender_settlement, options.receiver_settlement);
}

auto session::make_receiver(receiver_options options)
    -> std::expected<receiver_link, error>
{
    if (impl_->current != session_state::mapped)
        return std::unexpected(make_error(error_stage::protocol,
            errc::protocol_state,
            "session is not mapped"));
    if (impl_->next_handle > impl_->options.handle_max)
        return std::unexpected(make_error(error_stage::flow_control,
            errc::protocol_state,
            "session handle maximum reached"));
    return receiver_link::create(
        *impl_->owner, impl_->channel_id, impl_->next_handle++,
        std::move(options.name), std::move(options.source_terminus),
        options.sender_settlement, options.receiver_settlement);
}

auto session::make_transaction_controller()
    -> std::expected<transaction_controller, error>
{
    if (impl_->current != session_state::mapped)
        return std::unexpected(make_error(error_stage::transaction,
            errc::protocol_state,
            "session is not mapped"));
    if (impl_->next_handle > impl_->options.handle_max)
        return std::unexpected(make_error(error_stage::flow_control,
            errc::protocol_state,
            "session handle maximum reached"));
    return transaction_controller::create(*impl_->owner, impl_->channel_id,
        impl_->next_handle++);
}

auto session::end(cancel_token& token)
    -> task<std::expected<void, error>>
{
    if (impl_->current == session_state::ended)
        co_return {};
    auto sent = co_await impl_->owner->send(impl_->channel_id,
        performative{amqp10::end{}}, token);
    if (!sent)
        co_return std::unexpected(sent.error());
    impl_->current = session_state::end_sent;
    auto peer = co_await impl_->owner->receive(impl_->channel_id, token);
    if (!peer || !std::holds_alternative<amqp10::end>(*peer))
        co_return std::unexpected(
            peer ? make_error(error_stage::protocol,
                       errc::unexpected_performative, "expected peer End")
                 : peer.error());
    impl_->current = session_state::ended;
    co_return {};
}

auto session::state() const noexcept -> session_state
{
    return impl_->current;
}

auto session::channel() const noexcept -> std::uint16_t
{
    return impl_->channel_id;
}
} // namespace cnetmod::amqp10
