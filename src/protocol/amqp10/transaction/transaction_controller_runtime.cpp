module;
#include <cnetmod/config.hpp>
module cnetmod.protocol.amqp10;
import :transaction_controller;
import std;
import :performative_channel;
import :performative_model;
import :message_section;
import :amqp_value_codec;
import :performative_codec;
import :protocol_error;

namespace cnetmod::amqp10 {
struct transaction_controller::impl
{
    performative_channel* owner;
    std::uint16_t channel;
    std::uint32_t handle;
    bool attached = false;
    std::shared_ptr<std::atomic<std::uint32_t>> next_delivery_id;
};

transaction_controller::transaction_controller(std::unique_ptr<impl> p)
    : impl_(std::move(p)) {}

transaction_controller::~transaction_controller() = default;
transaction_controller::transaction_controller(
    transaction_controller&&) noexcept = default;
auto transaction_controller::operator=(transaction_controller&&) noexcept
    -> transaction_controller& = default;

auto transaction_controller::create(performative_channel& o, std::uint16_t c,
    std::uint32_t h,
    std::shared_ptr<std::atomic<std::uint32_t>> delivery_ids)
    -> transaction_controller
{
    return transaction_controller(
        std::make_unique<impl>(impl{&o, c, h, false, std::move(delivery_ids)}));
}

namespace {
    auto command_message(const performative& p) -> binary
    {
        auto wire = encode_performative(p);
        decoder input(wire);
        auto command = input.read_value();
        if (!command)
            return {};
        encoder output;
        output.write_value(
            value::described(descriptor{std::uint64_t{0x77}}, std::move(*command)));
        return output.release();
    }
} // namespace

auto transaction_controller::declare(cancel_token& token)
    -> task<std::expected<binary, error>>
{
    if (!impl_->attached)
    {
        attach a{.name = "cnetmod-txn-controller",
            .handle = impl_->handle,
            .link_role = role::sender,
            .snd_settle = sender_settle_mode::unsettled,
            .rcv_settle = receiver_settle_mode::first,
            .source_terminus = {},
            .target_terminus = {},
            .transaction_coordinator = true,
            .unsettled = {},
            .incomplete_unsettled = false,
            .initial_delivery_count = 0,
            .properties = {}};
        auto sent =
            co_await impl_->owner->send(impl_->channel, performative{a}, token);
        if (!sent)
            co_return std::unexpected(sent.error());
        std::vector<performative> deferred;
        while (true)
        {
            auto peer = co_await impl_->owner->receive(impl_->channel, token);
            if (!peer)
                co_return std::unexpected(peer.error());
            if (const auto* attached = std::get_if<attach>(&*peer);
                attached && attached->name == a.name)
            {
                impl_->owner->restore_received(
                    impl_->channel, std::move(deferred));
                break;
            }
            if (const auto* detached = std::get_if<detach>(&*peer);
                detached && detached->handle == impl_->handle)
                co_return std::unexpected(make_error(error_stage::transaction,
                    errc::transaction_failed,
                    "transaction coordinator link was detached"));
            if (const auto* credit = std::get_if<flow>(&*peer);
                credit && credit->handle == impl_->handle)
                continue;
            deferred.push_back(std::move(*peer));
        }
        impl_->attached = true;
    }
    auto id = impl_->next_delivery_id->fetch_add(
        1, std::memory_order_relaxed);
    transfer tx{.handle = impl_->handle,
        .delivery_id = id,
        .delivery_tag = binary{std::byte(id >> 24), std::byte(id >> 16),
            std::byte(id >> 8), std::byte(id)},
        .payload = command_message(performative{amqp10::declare{}})};
    auto sent = co_await impl_->owner->send(impl_->channel,
        performative{std::move(tx)}, token);
    if (!sent)
        co_return std::unexpected(sent.error());
    std::vector<performative> deferred;
    while (true)
    {
        auto peer = co_await impl_->owner->receive(impl_->channel, token);
        if (!peer)
            co_return std::unexpected(peer.error());
        if (auto d = std::get_if<disposition>(&*peer);
            d && d->first == id && d->state)
        {
            if (d->state->kind == outcome_kind::declared &&
                d->state->transaction_id)
            {
                impl_->owner->restore_received(
                    impl_->channel, std::move(deferred));
                co_return *d->state->transaction_id;
            }
            if (d->state->kind == outcome_kind::rejected)
            {
                impl_->owner->restore_received(
                    impl_->channel, std::move(deferred));
                co_return std::unexpected(make_error(error_stage::transaction,
                    errc::transaction_failed,
                    "transaction declaration rejected"));
            }
        }
        deferred.push_back(std::move(*peer));
    }
}

auto transaction_controller::discharge(
    std::span<const std::byte> transaction_id, bool fail, cancel_token& token)
    -> task<std::expected<void, error>>
{
    if (!impl_->attached)
        co_return std::unexpected(
            make_error(error_stage::transaction, errc::protocol_state,
                "transaction controller is not attached"));
    auto id = impl_->next_delivery_id->fetch_add(
        1, std::memory_order_relaxed);
    binary transaction(transaction_id.begin(), transaction_id.end());
    transfer tx{.handle = impl_->handle,
        .delivery_id = id,
        .delivery_tag = binary{std::byte(id >> 24), std::byte(id >> 16),
            std::byte(id >> 8), std::byte(id)},
        .payload = command_message(performative{
            amqp10::discharge{std::move(transaction), fail}})};
    auto sent = co_await impl_->owner->send(impl_->channel,
        performative{std::move(tx)}, token);
    if (!sent)
        co_return std::unexpected(sent.error());
    std::vector<performative> deferred;
    while (true)
    {
        auto peer = co_await impl_->owner->receive(impl_->channel, token);
        if (!peer)
            co_return std::unexpected(peer.error());
        if (auto d = std::get_if<disposition>(&*peer); d && d->first == id)
        {
            if (d->state && d->state->kind == outcome_kind::rejected)
            {
                impl_->owner->restore_received(
                    impl_->channel, std::move(deferred));
                co_return std::unexpected(make_error(
                    error_stage::transaction, errc::transaction_failed,
                    "transaction discharge rejected"));
            }
            impl_->owner->restore_received(
                impl_->channel, std::move(deferred));
            co_return {};
        }
        deferred.push_back(std::move(*peer));
    }
}
} // namespace cnetmod::amqp10
