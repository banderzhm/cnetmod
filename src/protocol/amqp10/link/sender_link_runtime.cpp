module;
#include <cnetmod/config.hpp>
module cnetmod.protocol.amqp10;
import :sender_link;
import std;
import :performative_channel;
import :recovery_observer;
import :performative_model;
import :performative_codec;
import :message_section;
import :protocol_error;

namespace cnetmod::amqp10 {
struct sender_link::impl : recovery_observer
{
    struct pending_transfer
    {
        binary tag;
        binary payload;
        send_options options;
    };

    performative_channel* owner{};
    std::uint16_t channel{};
    std::uint32_t handle{};
    std::string link_name;
    target terminus;
    sender_settle_mode snd = sender_settle_mode::mixed;
    receiver_settle_mode rcv = receiver_settle_mode::first;
    link_state current = link_state::detached;
    std::uint32_t available_credit = 0;
    std::uint32_t delivery_count = 0;
    std::uint32_t next_delivery_id = 0;
    std::map<std::uint32_t, pending_transfer> unsettled;
    std::map<std::uint32_t, delivery_outcome> completed_outcomes;

    void record_flow(const flow& update)
    {
        if (update.handle == handle)
            available_credit = update.link_credit.value_or(available_credit);
    }

    void record_disposition(const disposition& update)
    {
        if (update.disposition_role != role::receiver)
            return;
        const auto last = update.last.value_or(update.first);
        auto pending = unsettled.lower_bound(update.first);
        while (pending != unsettled.end() && pending->first <= last)
        {
            const auto id = pending->first;
            pending = unsettled.erase(pending);
            completed_outcomes.insert_or_assign(
                id, update.state.value_or(delivery_outcome{.kind = outcome_kind::accepted}));
        }
    }

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
            .link_role = role::sender,
            .snd_settle = snd,
            .rcv_settle = rcv,
            .target_terminus = terminus,
            .initial_delivery_count = delivery_count};
        for (const auto& [id, pending] : unsettled)
            request.unsettled.emplace_back(pending.tag, std::nullopt);
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
                           "expected sender Attach during recovery")
                     : peer.error());
        current = link_state::attached;
        const auto maximum = owner->maximum_frame_size();
        if (maximum <= 512)
            co_return std::unexpected(make_error(
                error_stage::flow_control, errc::frame_size_too_small,
                "peer max-frame-size leaves no resumed Transfer payload"));
        const auto chunk_size = static_cast<std::size_t>(maximum - 256);
        for (const auto& [id, pending] : unsettled)
        {
            std::size_t offset = 0;
            bool first = true;
            do
            {
                const auto count =
                    std::min(chunk_size, pending.payload.size() - offset);
                transfer resumed;
                resumed.handle = handle;
                resumed.delivery_id = first ? std::optional{id} : std::nullopt;
                resumed.delivery_tag = first ? pending.tag : binary{};
                resumed.settled = first && pending.options.settled;
                resumed.more = offset + count < pending.payload.size();
                resumed.resume = first;
                resumed.payload.assign(pending.payload.begin() +
                        static_cast<std::ptrdiff_t>(offset),
                    pending.payload.begin() +
                        static_cast<std::ptrdiff_t>(offset + count));
                if (first && pending.options.transaction_id)
                    resumed.state = delivery_outcome{.kind = outcome_kind::transactional,
                        .transaction_id =
                            *pending.options.transaction_id};
                auto replayed = co_await owner->send(
                    channel, performative{std::move(resumed)}, token);
                if (!replayed)
                    co_return std::unexpected(replayed.error());
                offset += count;
                first = false;
            } while (offset < pending.payload.size());
        }
        co_return {};
    }
};

sender_link::sender_link(std::unique_ptr<impl> p)
    : impl_(std::move(p)) {}

sender_link::~sender_link() = default;
sender_link::sender_link(sender_link&&) noexcept = default;
auto sender_link::operator=(sender_link&&) noexcept -> sender_link& = default;

auto sender_link::create(performative_channel& o, std::uint16_t c,
    std::uint32_t h, std::string n, target t,
    sender_settle_mode s, receiver_settle_mode r)
    -> sender_link
{
    auto state = std::make_unique<impl>();
    state->owner = &o;
    state->channel = c;
    state->handle = h;
    state->link_name = std::move(n);
    state->terminus = std::move(t);
    state->snd = s;
    state->rcv = r;
    o.register_recovery_observer(*state);
    return sender_link(std::move(state));
}

auto sender_link::attach(cancel_token& token)
    -> task<std::expected<void, error>>
{
    if (impl_->current != link_state::detached)
        co_return std::unexpected(make_error(error_stage::protocol,
            errc::protocol_state,
            "sender link is already attached"));
    cnetmod::amqp10::attach request{.name = impl_->link_name,
        .handle = impl_->handle,
        .link_role = role::sender,
        .snd_settle = impl_->snd,
        .rcv_settle = impl_->rcv,
        .target_terminus = impl_->terminus,
        .initial_delivery_count = impl_->delivery_count};
    auto sent =
        co_await impl_->owner->send(impl_->channel, performative{request}, token);
    if (!sent)
        co_return std::unexpected(sent.error());
    impl_->current = link_state::attach_sent;
    while (true)
    {
        auto peer = co_await impl_->owner->receive(impl_->channel, token);
        if (!peer)
            co_return std::unexpected(peer.error());
        if (auto a = std::get_if<amqp10::attach>(&*peer))
        {
            if (a->name != impl_->link_name)
                continue;
            impl_->current = link_state::attached;
            co_return {};
        }
        if (auto f = std::get_if<flow>(&*peer); f && f->handle == impl_->handle)
        {
            impl_->available_credit = f->link_credit.value_or(0);
            continue;
        }
        co_return std::unexpected(make_error(error_stage::protocol,
            errc::unexpected_performative,
            "expected peer Attach"));
    }
}

auto sender_link::begin_send(const message& m, send_options options,
    cancel_token& token)
    -> task<std::expected<std::uint32_t, error>>
{
    if (impl_->current != link_state::attached)
        co_return std::unexpected(make_error(error_stage::protocol,
            errc::protocol_state,
            "sender link is not attached"));
    while (impl_->available_credit == 0)
    {
        auto peer = co_await impl_->owner->receive(impl_->channel, token);
        if (!peer)
            co_return std::unexpected(peer.error());
        if (auto detached = std::get_if<cnetmod::amqp10::detach>(&*peer);
            detached && detached->handle == impl_->handle)
        {
            impl_->current =
                detached->closed ? link_state::closed : link_state::detached;
            co_return std::unexpected(make_error(error_stage::protocol,
                errc::connection_closed,
                "sender link detached while waiting for credit"));
        }
        if (auto disposition_value = std::get_if<disposition>(&*peer))
            impl_->record_disposition(*disposition_value);
        auto update = std::get_if<flow>(&*peer);
        if (!update)
            continue;
        impl_->record_flow(*update);
        if (update->handle == impl_->handle && update->drain &&
            impl_->available_credit == 0)
            co_return std::unexpected(make_error(error_stage::flow_control,
                errc::link_credit_exhausted,
                "peer drained sender credit"));
    }

    const auto id = impl_->next_delivery_id++;
    binary tag{std::byte(id >> 24), std::byte(id >> 16), std::byte(id >> 8),
        std::byte(id)};
    auto encoded = encode_message(m);
    if (!options.settled)
        impl_->unsettled.emplace(id, impl::pending_transfer{tag, encoded, options});
    const auto negotiated = impl_->owner->maximum_frame_size();
    if (negotiated <= 512)
    {
        impl_->unsettled.erase(id);
        co_return std::unexpected(make_error(
            error_stage::flow_control, errc::frame_size_too_small,
            "peer max-frame-size leaves no Transfer payload"));
    }
    const auto chunk_size = static_cast<std::size_t>(negotiated - 256);
    std::size_t offset = 0;
    bool first = true;
    do
    {
        const auto count = std::min(chunk_size, encoded.size() - offset);
        transfer request;
        request.handle = impl_->handle;
        request.delivery_id = first ? std::optional{id} : std::nullopt;
        request.delivery_tag = first ? tag : binary{};
        request.settled = first && options.settled;
        request.more = offset + count < encoded.size();
        request.payload.assign(
            encoded.begin() + static_cast<std::ptrdiff_t>(offset),
            encoded.begin() + static_cast<std::ptrdiff_t>(offset + count));
        if (first && options.transaction_id)
            request.state =
                delivery_outcome{.kind = outcome_kind::transactional,
                    .transaction_id = *options.transaction_id};
        auto sent = co_await impl_->owner->send(
            impl_->channel, performative{std::move(request)}, token);
        if (!sent)
        {
            impl_->unsettled.erase(id);
            co_return std::unexpected(sent.error());
        }
        offset += count;
        first = false;
    } while (offset < encoded.size());

    --impl_->available_credit;
    ++impl_->delivery_count;
    co_return id;
}

auto sender_link::await_outcome(std::uint32_t id, cancel_token& token)
    -> task<std::expected<send_result, error>>
{
    if (impl_->current != link_state::attached)
        co_return std::unexpected(make_error(error_stage::protocol,
            errc::protocol_state,
            "sender link is not attached"));
    if (!impl_->unsettled.contains(id) && !impl_->completed_outcomes.contains(id))
        co_return std::unexpected(make_error(error_stage::acknowledgement,
            errc::invalid_field,
            "delivery id is not awaiting a remote outcome"));
    while (true)
    {
        if (auto completed = impl_->completed_outcomes.find(id);
            completed != impl_->completed_outcomes.end())
        {
            auto outcome = std::move(completed->second);
            impl_->completed_outcomes.erase(completed);
            if (outcome.kind == outcome_kind::rejected)
                co_return std::unexpected(
                    make_error(error_stage::acknowledgement,
                        errc::delivery_rejected, "AMQP delivery rejected"));
            co_return send_result{id, std::move(outcome)};
        }
        auto peer = co_await impl_->owner->receive(impl_->channel, token);
        if (!peer)
            co_return std::unexpected(peer.error());
        if (auto detached = std::get_if<cnetmod::amqp10::detach>(&*peer);
            detached && detached->handle == impl_->handle)
        {
            impl_->current =
                detached->closed ? link_state::closed : link_state::detached;
            impl_->unsettled.erase(id);
            co_return std::unexpected(make_error(error_stage::protocol,
                errc::delivery_rejected,
                "sender link detached before the remote delivery outcome"));
        }
        if (auto disposition_value = std::get_if<disposition>(&*peer))
            impl_->record_disposition(*disposition_value);
        if (auto update = std::get_if<flow>(&*peer))
            impl_->record_flow(*update);
    }
}

auto sender_link::send(const message& m, send_options options,
    cancel_token& token)
    -> task<std::expected<send_result, error>>
{
    auto started = co_await begin_send(m, options, token);
    if (!started)
        co_return std::unexpected(started.error());
    if (options.settled)
        co_return send_result{*started,
            delivery_outcome{.kind = outcome_kind::accepted}};
    co_return co_await await_outcome(*started, token);
}

auto sender_link::detach(bool close_link, cancel_token& token)
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

auto sender_link::state() const noexcept -> link_state
{
    return impl_->current;
}

auto sender_link::credit() const noexcept -> std::uint32_t
{
    return impl_->available_credit;
}

auto sender_link::pending_unsettled_count() const noexcept -> std::size_t
{
    return impl_->unsettled.size();
}

auto sender_link::name() const noexcept -> std::string_view
{
    return impl_->link_name;
}
} // namespace cnetmod::amqp10
