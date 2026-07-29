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
    std::uint32_t next_delivery_id = 0;
};

transaction_controller::transaction_controller(std::unique_ptr<impl> p)
    : impl_(std::move(p)) {}

transaction_controller::~transaction_controller() = default;
transaction_controller::transaction_controller(
    transaction_controller&&) noexcept = default;
auto transaction_controller::operator=(transaction_controller&&) noexcept
    -> transaction_controller& = default;

auto transaction_controller::create(performative_channel& o, std::uint16_t c,
    std::uint32_t h) -> transaction_controller
{
    return transaction_controller(std::make_unique<impl>(impl{&o, c, h}));
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
            .transaction_coordinator = true};
        auto sent =
            co_await impl_->owner->send(impl_->channel, performative{a}, token);
        if (!sent)
            co_return std::unexpected(sent.error());
        auto peer = co_await impl_->owner->receive(impl_->channel, token);
        if (!peer || !std::holds_alternative<attach>(*peer))
            co_return std::unexpected(
                peer ? make_error(error_stage::transaction,
                           errc::unexpected_performative,
                           "expected transaction coordinator Attach")
                     : peer.error());
        impl_->attached = true;
    }
    auto id = impl_->next_delivery_id++;
    transfer tx{.handle = impl_->handle,
        .delivery_id = id,
        .delivery_tag = binary{std::byte(id >> 24), std::byte(id >> 16),
            std::byte(id >> 8), std::byte(id)},
        .payload = command_message(performative{amqp10::declare{}})};
    auto sent = co_await impl_->owner->send(impl_->channel,
        performative{std::move(tx)}, token);
    if (!sent)
        co_return std::unexpected(sent.error());
    while (true)
    {
        auto peer = co_await impl_->owner->receive(impl_->channel, token);
        if (!peer)
            co_return std::unexpected(peer.error());
        if (auto t = std::get_if<transfer>(&*peer);
            t && t->handle == impl_->handle)
        {
            auto decoded_message = decode_message(t->payload);
            if (decoded_message)
                if (auto body = std::get_if<value>(&decoded_message->body))
                {
                    encoder encoded;
                    encoded.write_value(*body);
                    auto response = decode_performative(encoded.bytes());
                    if (response && std::holds_alternative<declared>(*response))
                        co_return std::get<declared>(*response).transaction_id;
                }
        }
        if (auto d = std::get_if<disposition>(&*peer);
            d && d->first == id && d->state &&
            d->state->kind == outcome_kind::rejected)
            co_return std::unexpected(make_error(error_stage::transaction,
                errc::transaction_failed,
                "transaction declaration rejected"));
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
    auto id = impl_->next_delivery_id++;
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
    while (true)
    {
        auto peer = co_await impl_->owner->receive(impl_->channel, token);
        if (!peer)
            co_return std::unexpected(peer.error());
        if (auto d = std::get_if<disposition>(&*peer); d && d->first == id)
        {
            if (d->state && d->state->kind == outcome_kind::rejected)
                co_return std::unexpected(make_error(
                    error_stage::transaction, errc::transaction_failed,
                    "transaction discharge rejected"));
            co_return {};
        }
    }
}
} // namespace cnetmod::amqp10
