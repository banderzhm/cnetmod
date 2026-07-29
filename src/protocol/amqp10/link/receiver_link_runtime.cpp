module;
#include <cnetmod/config.hpp>
module cnetmod.protocol.amqp10;
import :receiver_link;
import std;
import :performative_channel;
import :recovery_observer;
import :performative_model;
import :message_section;
import :protocol_error;

namespace cnetmod::amqp10 {
struct receiver_link::impl : recovery_observer
{
    performative_channel* owner{};
    std::uint16_t channel{};
    std::uint32_t handle{};
    std::string link_name;
    source terminus;
    sender_settle_mode snd = sender_settle_mode::mixed;
    receiver_settle_mode rcv = receiver_settle_mode::first;
    link_state current = link_state::detached;
    std::uint32_t available_credit = 0;
    std::uint32_t delivery_count = 0;
    std::map<std::uint32_t, binary> unsettled;

    ~impl()
    {
        if (owner)
            owner->unregister_recovery_observer(*this);
    }

    auto recovery_order() const noexcept -> std::uint8_t override
    {
        return 2;
    }

    auto recover(cancel_token& token)
        -> task<std::expected<void, error>> override
    {
        if (current == link_state::detached || current == link_state::closed)
            co_return {};
        cnetmod::amqp10::attach request{.name = link_name,
            .handle = handle,
            .link_role = role::receiver,
            .snd_settle = snd,
            .rcv_settle = rcv,
            .source_terminus = terminus,
            .initial_delivery_count = delivery_count};
        for (const auto& [id, tag] : unsettled)
            request.unsettled.emplace_back(tag, std::nullopt);
        auto sent = co_await owner->send(channel, performative{request}, token);
        if (!sent)
            co_return std::unexpected(sent.error());
        current = link_state::attach_sent;
        auto peer = co_await owner->receive(channel, token);
        if (!peer ||
            !std::holds_alternative<cnetmod::amqp10::attach>(*peer))
            co_return std::unexpected(
                peer ? make_error(error_stage::protocol,
                           errc::unexpected_performative,
                           "expected receiver Attach during recovery")
                     : peer.error());
        current = link_state::attached;
        flow restored{.incoming_window = 2048,
            .next_outgoing_id = 1,
            .outgoing_window = 2048,
            .handle = handle,
            .delivery_count = delivery_count,
            .link_credit = available_credit};
        co_return co_await owner->send(channel, performative{restored}, token);
    }
};

receiver_link::receiver_link(std::unique_ptr<impl> p)
    : impl_(std::move(p)) {}

receiver_link::~receiver_link() = default;
receiver_link::receiver_link(receiver_link&&) noexcept = default;
auto receiver_link::operator=(receiver_link&&) noexcept
    -> receiver_link& = default;

auto receiver_link::create(performative_channel& o, std::uint16_t c,
    std::uint32_t h, std::string n, source s,
    sender_settle_mode sm, receiver_settle_mode rm)
    -> receiver_link
{
    auto state = std::make_unique<impl>();
    state->owner = &o;
    state->channel = c;
    state->handle = h;
    state->link_name = std::move(n);
    state->terminus = std::move(s);
    state->snd = sm;
    state->rcv = rm;
    o.register_recovery_observer(*state);
    return receiver_link(std::move(state));
}

auto receiver_link::attach(std::uint32_t initial_credit, cancel_token& token)
    -> task<std::expected<void, error>>
{
    if (impl_->current != link_state::detached)
        co_return std::unexpected(make_error(error_stage::protocol,
            errc::protocol_state,
            "receiver link is already attached"));
    cnetmod::amqp10::attach request{.name = impl_->link_name,
        .handle = impl_->handle,
        .link_role = role::receiver,
        .snd_settle = impl_->snd,
        .rcv_settle = impl_->rcv,
        .source_terminus = impl_->terminus};
    auto sent =
        co_await impl_->owner->send(impl_->channel, performative{request}, token);
    if (!sent)
        co_return std::unexpected(sent.error());
    impl_->current = link_state::attach_sent;
    auto peer = co_await impl_->owner->receive(impl_->channel, token);
    if (!peer || !std::holds_alternative<amqp10::attach>(*peer))
        co_return std::unexpected(
            peer ? make_error(error_stage::protocol,
                       errc::unexpected_performative, "expected peer Attach")
                 : peer.error());
    impl_->current = link_state::attached;
    co_return co_await add_credit(initial_credit, false, token);
}

auto receiver_link::add_credit(std::uint32_t amount, bool drain,
    cancel_token& token)
    -> task<std::expected<void, error>>
{
    if (impl_->current != link_state::attached)
        co_return std::unexpected(make_error(error_stage::protocol,
            errc::protocol_state,
            "receiver link is not attached"));
    if (std::numeric_limits<std::uint32_t>::max() - impl_->available_credit <
        amount)
        co_return std::unexpected(make_error(error_stage::flow_control,
            errc::invalid_field,
            "link credit overflow"));
    impl_->available_credit += amount;
    flow update{.incoming_window = 2048,
        .next_outgoing_id = 1,
        .outgoing_window = 2048,
        .handle = impl_->handle,
        .delivery_count = impl_->delivery_count,
        .link_credit = impl_->available_credit,
        .drain = drain};
    co_return co_await impl_->owner->send(impl_->channel, performative{update},
        token);
}

auto receiver_link::receive(cancel_token& token)
    -> task<std::expected<received_message, error>>
{
    if (impl_->current != link_state::attached)
        co_return std::unexpected(make_error(error_stage::protocol,
            errc::protocol_state,
            "receiver link is not attached"));
    if (impl_->available_credit == 0)
        co_return std::unexpected(make_error(error_stage::flow_control,
            errc::link_credit_exhausted,
            "receiver link has no credit"));
    received_message result;
    binary payload;
    bool started = false;
    while (true)
    {
        auto peer = co_await impl_->owner->receive(impl_->channel, token);
        if (!peer)
            co_return std::unexpected(peer.error());
        if (auto incoming = std::get_if<transfer>(&*peer);
            incoming && incoming->handle == impl_->handle)
        {
            if (!started)
            {
                result.delivery_id =
                    incoming->delivery_id.value_or(impl_->delivery_count);
                result.delivery_tag = incoming->delivery_tag;
                result.settled = incoming->settled;
                result.resumed = incoming->resume;
                started = true;
            }
            if (incoming->aborted)
            {
                --impl_->available_credit;
                ++impl_->delivery_count;
                co_return std::unexpected(make_error(error_stage::protocol,
                    errc::delivery_rejected,
                    "peer aborted AMQP transfer"));
            }
            payload.insert(payload.end(), incoming->payload.begin(),
                incoming->payload.end());
            if (incoming->more)
                continue;
            auto decoded = decode_message(payload);
            if (!decoded)
                co_return std::unexpected(
                    error{.stage = error_stage::protocol,
                        .code = decoded.error(),
                        .message = "cannot decode AMQP message"});
            result.payload = std::move(*decoded);
            --impl_->available_credit;
            ++impl_->delivery_count;
            if (!result.settled)
                impl_->unsettled[result.delivery_id] = result.delivery_tag;
            co_return result;
        }
        if (auto detached = std::get_if<cnetmod::amqp10::detach>(&*peer);
            detached && detached->handle == impl_->handle)
        {
            impl_->current =
                detached->closed ? link_state::closed : link_state::detached;
            co_return std::unexpected(make_error(error_stage::protocol,
                errc::connection_closed,
                "receiver link detached by peer"));
        }
    }
}

auto receiver_link::settle(std::uint32_t id, delivery_outcome outcome,
    cancel_token& token)
    -> task<std::expected<void, error>>
{
    if (impl_->current != link_state::attached)
        co_return std::unexpected(
            make_error(error_stage::acknowledgement,
                errc::protocol_state, "receiver link is not attached"));
    disposition update{.disposition_role = role::receiver,
        .first = id,
        .settled = true,
        .state = std::move(outcome)};
    auto sent = co_await impl_->owner->send(
        impl_->channel, performative{std::move(update)}, token);
    if (sent)
        impl_->unsettled.erase(id);
    co_return sent;
}

auto receiver_link::detach(bool close_link, cancel_token& token)
    -> task<std::expected<void, error>>
{
    if (impl_->current == link_state::closed)
        co_return {};
    auto sent = co_await impl_->owner->send(
        impl_->channel,
        performative{cnetmod::amqp10::detach{.handle = impl_->handle,
            .closed = close_link}},
        token);
    if (!sent)
        co_return std::unexpected(sent.error());
    impl_->current = link_state::detach_sent;
    auto peer = co_await impl_->owner->receive(impl_->channel, token);
    if (!peer ||
        !std::holds_alternative<cnetmod::amqp10::detach>(*peer))
        co_return std::unexpected(
            peer ? make_error(error_stage::protocol,
                       errc::unexpected_performative, "expected peer Detach")
                 : peer.error());
    impl_->current = close_link ? link_state::closed : link_state::detached;
    co_return {};
}

auto receiver_link::state() const noexcept -> link_state
{
    return impl_->current;
}

auto receiver_link::credit() const noexcept -> std::uint32_t
{
    return impl_->available_credit;
}

auto receiver_link::name() const noexcept -> std::string_view
{
    return impl_->link_name;
}
} // namespace cnetmod::amqp10
