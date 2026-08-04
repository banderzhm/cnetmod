module;
#include <cnetmod/config.hpp>
module cnetmod.protocol.amqp091;
import :logical_channel;
import std;
import cnetmod.coro.task;
import :protocol_constants;
import :wire_frame_codec;
import :field_table_codec;
import :channel_options;
import :message_delivery;
import :publisher_confirm;
import :topology_recovery;
import :protocol_connection;

namespace cnetmod::amqp091 {
namespace {
    class writer
    {
    public:
        void u8(std::uint8_t v)
        {
            data.push_back(static_cast<std::byte>(v));
        }

        template <class T> void integer(T v)
        {
            for (std::size_t s = sizeof(T); s-- > 0;)
                u8(static_cast<std::uint8_t>(v >> (s * 8)));
        }

        void bytes(std::span<const std::byte> v)
        {
            data.insert(data.end(), v.begin(), v.end());
        }

        void short_string(std::string_view v)
        {
            u8(static_cast<std::uint8_t>(v.size()));
            bytes(std::as_bytes(std::span{v.data(), v.size()}));
        }

        std::vector<std::byte> data;
    };

    class reader
    {
    public:
        explicit reader(std::span<const std::byte> v)
            : data(v) {}

        auto u8() -> std::optional<std::uint8_t>
        {
            if (pos == data.size())
                return {};
            return std::to_integer<std::uint8_t>(data[pos++]);
        }

        template <class T> auto integer() -> std::optional<T>
        {
            if (data.size() - pos < sizeof(T))
                return {};
            T v{};
            for (std::size_t i = 0; i < sizeof(T); ++i)
                v = static_cast<T>((v << 8) | *u8());
            return v;
        }

        auto bytes(std::size_t n) -> std::optional<std::span<const std::byte>>
        {
            if (data.size() - pos < n)
                return {};
            auto v = data.subspan(pos, n);
            pos += n;
            return v;
        }

        auto short_string() -> std::optional<std::string>
        {
            auto n = u8();
            if (!n)
                return {};
            auto v = bytes(*n);
            if (!v)
                return {};
            return std::string(reinterpret_cast<const char*>(v->data()), v->size());
        }

        std::span<const std::byte> data;
        std::size_t pos = 0;
    };

    auto append_table(writer& out, const field_table& table) -> result<void>
    {
        auto encoded = encode_field_table(table);
        if (!encoded)
            return std::unexpected(encoded.error());
        out.bytes(*encoded);
        return {};
    }

    auto malformed_reply(std::string text) -> error
    {
        return make_error(error_code::malformed_frame, std::move(text));
    }
} // namespace

struct logical_channel::impl
{
    impl(std::shared_ptr<protocol_connection> c, std::uint16_t n)
        : connection(std::move(c)), number(n), confirms(connection->confirm_tracker(n)) {}

    std::shared_ptr<protocol_connection> connection;
    std::uint16_t number;
    bool open = true;
    bool confirm_mode = false;
    bool transaction = false;
    std::shared_ptr<publisher_confirm_tracker> confirms;
};

logical_channel::logical_channel(std::shared_ptr<protocol_connection> c,
    std::uint16_t n)
    : impl_(std::make_unique<impl>(std::move(c), n)) {}

logical_channel::~logical_channel() = default;

auto logical_channel::number() const noexcept -> std::uint16_t
{
    return impl_->number;
}

auto logical_channel::is_open() const noexcept -> bool
{
    return impl_->open;
}

auto logical_channel::async_close(std::string text) -> task<result<void>>
{
    if (!impl_->open)
        co_return result<void>{};
    writer out;
    out.integer<std::uint16_t>(200);
    out.short_string(text);
    out.integer<std::uint16_t>(0);
    out.integer<std::uint16_t>(0);
    auto r =
        co_await impl_->connection->async_rpc({.channel = impl_->number,
                                                  .class_id = 20,
                                                  .method_id = 40,
                                                  .arguments = std::move(out.data)},
            20, 41);
    if (!r)
        co_return std::unexpected(r.error());
    impl_->open = false;
    co_return result<void>{};
}

auto logical_channel::async_declare_exchange(exchange_declare_options o,
    field_table args)
    -> task<result<void>>
{
    writer out;
    out.integer<std::uint16_t>(0);
    out.short_string(o.name);
    out.short_string(exchange_type_name(o));
    out.u8((o.passive ? 1 : 0) | (o.durable ? 2 : 0) | (o.auto_delete ? 4 : 0) |
        (o.internal ? 8 : 0) | (o.no_wait ? 16 : 0));
    if (auto r = append_table(out, args); !r)
        co_return r;
    method_frame method{impl_->number, 40, 10, std::move(out.data)};
    if (o.no_wait)
    {
        auto f = encode_method(method);
        if (!f)
            co_return std::unexpected(f.error());
        if (auto r = co_await impl_->connection->async_send(std::move(*f)); !r)
            co_return r;
    }
    else
    {
        auto r = co_await impl_->connection->async_rpc(std::move(method), 40, 11);
        if (!r)
            co_return std::unexpected(r.error());
    }
    impl_->connection->topology()->remember(
        recorded_exchange{std::move(o), std::move(args)});
    co_return result<void>{};
}

auto logical_channel::async_delete_exchange(std::string name, bool unused,
    bool no_wait)
    -> task<result<void>>
{
    writer out;
    out.integer<std::uint16_t>(0);
    out.short_string(name);
    out.u8((unused ? 1 : 0) | (no_wait ? 2 : 0));
    method_frame m{impl_->number, 40, 20, std::move(out.data)};
    if (no_wait)
    {
        auto f = encode_method(m);
        if (!f)
            co_return std::unexpected(f.error());
        if (auto sent = co_await impl_->connection->async_send(std::move(*f));
            !sent)
            co_return sent;
    }
    else
    {
        auto r = co_await impl_->connection->async_rpc(std::move(m), 40, 21);
        if (!r)
            co_return std::unexpected(r.error());
    }
    impl_->connection->topology()->forget_exchange(name);
    co_return result<void>{};
}

auto logical_channel::async_declare_queue(queue_declare_options o,
    field_table args)
    -> task<result<queue_declare_result>>
{
    writer out;
    out.integer<std::uint16_t>(0);
    out.short_string(o.name);
    out.u8((o.passive ? 1 : 0) | (o.durable ? 2 : 0) | (o.exclusive ? 4 : 0) |
        (o.auto_delete ? 8 : 0) | (o.no_wait ? 16 : 0));
    if (auto r = append_table(out, args); !r)
        co_return std::unexpected(r.error());
    method_frame m{impl_->number, 50, 10, std::move(out.data)};
    if (o.no_wait)
    {
        auto requested = o.name;
        auto f = encode_method(m);
        if (!f)
            co_return std::unexpected(f.error());
        if (auto r = co_await impl_->connection->async_send(std::move(*f)); !r)
            co_return std::unexpected(r.error());
        impl_->connection->topology()->remember(
            recorded_queue{std::move(o), std::move(args), requested});
        co_return queue_declare_result{.name = std::move(requested)};
    }
    auto reply = co_await impl_->connection->async_rpc(std::move(m), 50, 11);
    if (!reply)
        co_return std::unexpected(reply.error());
    reader in(reply->arguments);
    auto name = in.short_string();
    auto messages = in.integer<std::uint32_t>();
    auto consumers = in.integer<std::uint32_t>();
    if (!name || !messages || !consumers)
        co_return std::unexpected(malformed_reply("truncated Queue.Declare-Ok"));
    impl_->connection->topology()->remember(
        recorded_queue{o, std::move(args), *name});
    co_return queue_declare_result{*name, *messages, *consumers};
}

auto logical_channel::async_delete_queue(std::string name, bool unused,
    bool empty, bool no_wait)
    -> task<result<std::uint32_t>>
{
    writer out;
    out.integer<std::uint16_t>(0);
    out.short_string(name);
    out.u8((unused ? 1 : 0) | (empty ? 2 : 0) | (no_wait ? 4 : 0));
    method_frame m{impl_->number, 50, 40, std::move(out.data)};
    if (no_wait)
    {
        auto f = encode_method(m);
        if (!f)
            co_return std::unexpected(f.error());
        if (auto r = co_await impl_->connection->async_send(std::move(*f)); !r)
            co_return std::unexpected(r.error());
        impl_->connection->topology()->forget_queue(name);
        co_return 0;
    }
    auto reply = co_await impl_->connection->async_rpc(std::move(m), 50, 41);
    if (!reply)
        co_return std::unexpected(reply.error());
    reader in(reply->arguments);
    auto count = in.integer<std::uint32_t>();
    if (!count)
        co_return std::unexpected(malformed_reply("truncated Queue.Delete-Ok"));
    impl_->connection->topology()->forget_queue(name);
    co_return *count;
}

auto logical_channel::async_purge_queue(std::string name, bool no_wait)
    -> task<result<std::uint32_t>>
{
    writer out;
    out.integer<std::uint16_t>(0);
    out.short_string(name);
    out.u8(no_wait ? 1 : 0);
    method_frame m{impl_->number, 50, 30, std::move(out.data)};
    if (no_wait)
    {
        auto f = encode_method(m);
        if (!f)
            co_return std::unexpected(f.error());
        if (auto r = co_await impl_->connection->async_send(std::move(*f)); !r)
            co_return std::unexpected(r.error());
        co_return 0;
    }
    auto reply = co_await impl_->connection->async_rpc(std::move(m), 50, 31);
    if (!reply)
        co_return std::unexpected(reply.error());
    reader in(reply->arguments);
    auto count = in.integer<std::uint32_t>();
    if (!count)
        co_return std::unexpected(malformed_reply("truncated Queue.Purge-Ok"));
    co_return *count;
}

auto logical_channel::async_bind_queue(binding_options o, field_table args)
    -> task<result<void>>
{
    writer out;
    out.integer<std::uint16_t>(0);
    out.short_string(o.queue);
    out.short_string(o.exchange);
    out.short_string(o.routing_key);
    out.u8(o.no_wait ? 1 : 0);
    if (auto r = append_table(out, args); !r)
        co_return r;
    method_frame m{impl_->number, 50, 20, std::move(out.data)};
    if (o.no_wait)
    {
        auto f = encode_method(m);
        if (!f)
            co_return std::unexpected(f.error());
        if (auto r = co_await impl_->connection->async_send(std::move(*f)); !r)
            co_return r;
    }
    else
    {
        auto r = co_await impl_->connection->async_rpc(std::move(m), 50, 21);
        if (!r)
            co_return std::unexpected(r.error());
    }
    impl_->connection->topology()->remember(
        recorded_binding{std::move(o), std::move(args)});
    co_return result<void>{};
}

auto logical_channel::async_unbind_queue(binding_options o, field_table args)
    -> task<result<void>>
{
    writer out;
    out.integer<std::uint16_t>(0);
    out.short_string(o.queue);
    out.short_string(o.exchange);
    out.short_string(o.routing_key);
    if (auto r = append_table(out, args); !r)
        co_return r;
    auto reply =
        co_await impl_->connection->async_rpc({.channel = impl_->number,
                                                  .class_id = 50,
                                                  .method_id = 50,
                                                  .arguments = std::move(out.data)},
            50, 51);
    if (!reply)
        co_return std::unexpected(reply.error());
    impl_->connection->topology()->forget_binding(o);
    co_return result<void>{};
}

auto logical_channel::async_set_qos(qos_options o) -> task<result<void>>
{
    writer out;
    out.integer(o.prefetch_size);
    out.integer(o.prefetch_count);
    out.u8(o.global ? 1 : 0);
    auto reply =
        co_await impl_->connection->async_rpc({.channel = impl_->number,
                                                  .class_id = 60,
                                                  .method_id = 10,
                                                  .arguments = std::move(out.data)},
            60, 11);
    if (!reply)
        co_return std::unexpected(reply.error());
    co_return result<void>{};
}

auto logical_channel::async_publish(publish_options o,
    message message)
    -> task<result<std::uint64_t>>
{
    writer out;
    out.integer<std::uint16_t>(0);
    out.short_string(o.exchange);
    out.short_string(o.routing_key);
    out.u8((o.mandatory ? 1 : 0) | (o.immediate ? 2 : 0));
    auto tag = impl_->confirm_mode ? impl_->confirms->reserve_sequence() : 0;
    if (auto r = co_await impl_->connection->async_send_message(
            impl_->number,
            {.channel = impl_->number,
                .class_id = 60,
                .method_id = 40,
                .arguments = std::move(out.data)},
            std::move(message));
        !r)
        co_return std::unexpected(r.error());
    co_return tag;
}

auto logical_channel::async_consume(consume_options o, delivery_handler handler,
    field_table args)
    -> task<result<std::string>>
{
    writer out;
    out.integer<std::uint16_t>(0);
    out.short_string(o.queue);
    out.short_string(o.consumer_tag);
    out.u8((o.no_local ? 1 : 0) | (o.no_ack ? 2 : 0) | (o.exclusive ? 4 : 0) |
        (o.no_wait ? 8 : 0));
    if (auto r = append_table(out, args); !r)
        co_return std::unexpected(r.error());
    method_frame m{impl_->number, 60, 20, std::move(out.data)};
    std::string tag = o.consumer_tag;
    if (o.no_wait)
    {
        if (tag.empty())
            co_return std::unexpected(
                make_error(error_code::precondition_failed,
                    "consumer_tag required with no_wait"));
        auto f = encode_method(m);
        if (!f)
            co_return std::unexpected(f.error());
        if (auto r = co_await impl_->connection->async_send(std::move(*f)); !r)
            co_return std::unexpected(r.error());
    }
    else
    {
        auto reply = co_await impl_->connection->async_rpc(std::move(m), 60, 21);
        if (!reply)
            co_return std::unexpected(reply.error());
        reader in(reply->arguments);
        auto assigned = in.short_string();
        if (!assigned)
            co_return std::unexpected(malformed_reply("truncated Basic.Consume-Ok"));
        tag = std::move(*assigned);
    }
    o.consumer_tag = tag;
    auto recovery_handler = handler;
    impl_->connection->register_delivery_handler(impl_->number, tag,
        std::move(handler));
    impl_->connection->topology()->remember(recorded_consumer{
        std::move(o), std::move(args), std::move(recovery_handler)});
    co_return tag;
}

auto logical_channel::async_cancel_consumer(std::string tag, bool no_wait)
    -> task<result<void>>
{
    writer out;
    out.short_string(tag);
    out.u8(no_wait ? 1 : 0);
    method_frame m{impl_->number, 60, 30, std::move(out.data)};
    if (no_wait)
    {
        auto f = encode_method(m);
        if (!f)
            co_return std::unexpected(f.error());
        if (auto r = co_await impl_->connection->async_send(std::move(*f)); !r)
            co_return r;
    }
    else
    {
        auto r = co_await impl_->connection->async_rpc(std::move(m), 60, 31);
        if (!r)
            co_return std::unexpected(r.error());
    }
    impl_->connection->unregister_delivery_handler(impl_->number, tag);
    impl_->connection->topology()->forget_consumer(tag);
    co_return result<void>{};
}

auto logical_channel::async_ack(std::uint64_t tag, bool multiple)
    -> task<result<void>>
{
    writer out;
    out.integer(tag);
    out.u8(multiple ? 1 : 0);
    auto f = encode_method({.channel = impl_->number,
        .class_id = 60,
        .method_id = 80,
        .arguments = std::move(out.data)});
    if (!f)
        co_return std::unexpected(f.error());
    co_return co_await impl_->connection->async_send(std::move(*f));
}

auto logical_channel::async_nack(std::uint64_t tag, bool multiple, bool requeue)
    -> task<result<void>>
{
    writer out;
    out.integer(tag);
    out.u8((multiple ? 1 : 0) | (requeue ? 2 : 0));
    auto f = encode_method({.channel = impl_->number,
        .class_id = 60,
        .method_id = 120,
        .arguments = std::move(out.data)});
    if (!f)
        co_return std::unexpected(f.error());
    co_return co_await impl_->connection->async_send(std::move(*f));
}

auto logical_channel::async_reject(std::uint64_t tag, bool requeue)
    -> task<result<void>>
{
    writer out;
    out.integer(tag);
    out.u8(requeue ? 1 : 0);
    auto f = encode_method({.channel = impl_->number,
        .class_id = 60,
        .method_id = 90,
        .arguments = std::move(out.data)});
    if (!f)
        co_return std::unexpected(f.error());
    co_return co_await impl_->connection->async_send(std::move(*f));
}

auto logical_channel::async_recover(bool requeue) -> task<result<void>>
{
    writer out;
    out.u8(requeue ? 1 : 0);
    auto reply =
        co_await impl_->connection->async_rpc({.channel = impl_->number,
                                                  .class_id = 60,
                                                  .method_id = 110,
                                                  .arguments = std::move(out.data)},
            60, 111);
    if (!reply)
        co_return std::unexpected(reply.error());
    co_return result<void>{};
}

auto logical_channel::async_enable_confirms(bool no_wait)
    -> task<result<void>>
{
    if (impl_->transaction)
        co_return std::unexpected(make_error(
            error_code::precondition_failed,
            "publisher confirms and transactions are mutually exclusive"));
    writer out;
    out.u8(no_wait ? 1 : 0);
    method_frame m{impl_->number, 85, 10, std::move(out.data)};
    if (no_wait)
    {
        auto f = encode_method(m);
        if (!f)
            co_return std::unexpected(f.error());
        if (auto r = co_await impl_->connection->async_send(std::move(*f)); !r)
            co_return r;
    }
    else
    {
        auto r = co_await impl_->connection->async_rpc(std::move(m), 85, 11);
        if (!r)
            co_return std::unexpected(r.error());
    }
    impl_->confirm_mode = true;
    co_return result<void>{};
}

void logical_channel::observe_confirms(
    std::weak_ptr<publisher_confirm_observer> o)
{
    impl_->confirms->observe(std::move(o));
}

auto logical_channel::async_select_transaction() -> task<result<void>>
{
    if (impl_->confirm_mode)
        co_return std::unexpected(make_error(
            error_code::precondition_failed,
            "transactions and publisher confirms are mutually exclusive"));
    auto r = co_await impl_->connection->async_rpc(
        {.channel = impl_->number, .class_id = 90, .method_id = 10, .arguments = {}}, 90, 11);
    if (!r)
        co_return std::unexpected(r.error());
    impl_->transaction = true;
    co_return result<void>{};
}

auto logical_channel::async_commit_transaction() -> task<result<void>>
{
    if (!impl_->transaction)
        co_return std::unexpected(make_error(error_code::precondition_failed,
            "transaction mode is not selected"));
    auto r = co_await impl_->connection->async_rpc(
        {.channel = impl_->number, .class_id = 90, .method_id = 20, .arguments = {}}, 90, 21);
    if (!r)
        co_return std::unexpected(r.error());
    co_return result<void>{};
}

auto logical_channel::async_rollback_transaction() -> task<result<void>>
{
    if (!impl_->transaction)
        co_return std::unexpected(make_error(error_code::precondition_failed,
            "transaction mode is not selected"));
    auto r = co_await impl_->connection->async_rpc(
        {.channel = impl_->number, .class_id = 90, .method_id = 30, .arguments = {}}, 90, 31);
    if (!r)
        co_return std::unexpected(r.error());
    co_return result<void>{};
}
} // namespace cnetmod::amqp091
