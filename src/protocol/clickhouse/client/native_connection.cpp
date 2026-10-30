module;
#include <cnetmod/config.hpp>
module cnetmod.protocol.clickhouse;
import std;
import cnetmod.core.socket;
import cnetmod.core.buffer;
import cnetmod.core.dns;
import cnetmod.coro.timer;
import cnetmod.executor.async_op;
#ifdef CNETMOD_HAS_SSL
import cnetmod.core.ssl;
#endif
#include "../block/native_block.hpp"
#include "../client/native_session.hpp"
#include "../wire/native_buffer.hpp"
#include "../wire/server_packet.hpp"

namespace cnetmod::clickhouse {
using detail::packet;
using detail::parse_packet;
using detail::server_packet;
using detail::transport_error;

client::client(io_context& context) : impl_(std::make_unique<implementation>(context)) {}

client::~client() = default;

void client::close() noexcept
{
    impl_->close();
}

auto client::is_open() const noexcept -> bool
{
    return impl_->connected;
}

auto client::info() const noexcept -> const server_info&
{
    return impl_->server;
}

auto client::connect(connection_options options, cancel_token* cancellation) -> task<std::expected<server_info, error>>
{
    cancel_token local;
    auto& token = cancellation ? *cancellation : local;
    try
    {
        // Precancellation and overlap must not damage an established session.
        impl_->check_cancel(token);
        implementation::operation operation{*impl_};
        if ((options.revision != 54401 && options.revision != protocol_revision) || options.host.empty() || !options.port || options.bounds.max_packet_bytes < 64 ||
            options.bounds.max_packet_bytes > 1024ULL * 1024 * 1024 || !options.bounds.max_rows ||
            !options.bounds.max_columns || !options.bounds.max_values || !options.bounds.max_depth ||
            options.bounds.max_depth > 128 || options.connect_timeout.count() <= 0 || options.operation_timeout.count() <= 0)
            detail::fail("invalid ClickHouse connection options", error_kind::invalid_argument);
        impl_->close();
        impl_->server = {};
        impl_->options = std::move(options);
        impl_->expires = deadline::after(impl_->options.operation_timeout);
        happy_eyeballs_options connect_options;
        connect_options.connect_timeout = impl_->options.connect_timeout;
        connect_options.socket_opts.no_delay = true;
        auto connected = co_await with_deadline(impl_->context, impl_->expires,
            async_connect_happy_eyeballs(impl_->context, impl_->options.host, impl_->options.port, connect_options, token), token);
        impl_->check_cancel(token);
        if (!connected)
            throw detail::failure{transport_error(connected.error())};
        impl_->transport = std::move(connected->sock);
        impl_->revision = impl_->options.revision;
        if (impl_->options.tls)
        {
#ifdef CNETMOD_HAS_SSL
            auto context = ssl_context::client();
            if (!context)
                throw detail::failure{transport_error(context.error())};
            impl_->tls_context = std::make_unique<ssl_context>(std::move(*context));
            impl_->tls_context->set_verify_peer(true);
            auto loaded = impl_->options.ca_file.empty() ? impl_->tls_context->set_default_ca() : impl_->tls_context->load_ca_file(impl_->options.ca_file);
            if (!loaded)
                throw detail::failure{transport_error(loaded.error())};
            impl_->tls_stream = std::make_unique<ssl_stream>(*impl_->tls_context, impl_->context, impl_->transport);
            impl_->tls_stream->set_connect_state();
            impl_->tls_stream->set_hostname(impl_->options.host);
            auto handshake = co_await with_deadline(impl_->context, impl_->expires, impl_->tls_stream->async_handshake(token), token);
            impl_->check_cancel(token);
            if (!handshake)
                throw detail::failure{transport_error(handshake.error())};
#else
            detail::fail("ClickHouse TLS requires CNETMOD_ENABLE_SSL", error_kind::invalid_argument);
#endif
        }
        detail::writer hello{{}, impl_->options.bounds};
        hello.varint(0);
        hello.string("cnetmod");
        hello.varint(2);
        hello.varint(2);
        hello.varint(impl_->options.revision);
        hello.string(impl_->options.database);
        hello.string(impl_->options.user);
        hello.string(impl_->options.password);
        co_await impl_->write(hello.output, token);
        auto response = co_await impl_->receive(token);
        if (response.kind != server_packet::hello)
            detail::fail("expected ClickHouse Hello");
        if (impl_->revision >= 54458)
        {
            detail::writer addendum{{}, impl_->options.bounds};
            addendum.string(""); // Quota key is part of the handshake, flushed immediately.
            co_await impl_->write(addendum.output, token);
        }
        impl_->server = std::move(response.hello);
        impl_->connected = true;
        operation.complete = true;
        co_return impl_->server;
    }
    catch (detail::failure& failure)
    {
        co_return std::unexpected(std::move(failure.value));
    }
}

auto client::ping(cancel_token* cancellation) -> task<std::expected<void, error>>
{
    cancel_token local;
    auto& token = cancellation ? *cancellation : local;
    try
    {
        impl_->check_cancel(token);
        implementation::operation operation{*impl_};
        if (!impl_->connected)
            detail::fail("ClickHouse client is not connected", error_kind::invalid_argument);
        const std::array bytes{std::byte{4}};
        co_await impl_->write(bytes, token);
        if ((co_await impl_->receive(token)).kind != server_packet::pong)
            detail::fail("expected ClickHouse Pong");
        operation.complete = true;
        co_return std::expected<void, error>{};
    }
    catch (detail::failure& failure)
    {
        co_return std::unexpected(std::move(failure.value));
    }
}
} // namespace cnetmod::clickhouse
