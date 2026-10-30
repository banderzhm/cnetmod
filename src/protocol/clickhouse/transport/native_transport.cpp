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

namespace detail {
    auto transport_error(std::error_code code) -> error
    {
        return {.kind = code == std::errc::operation_canceled ? error_kind::cancelled : error_kind::transport,
            .cause = code,
            .message = code.message()};
    }
} // namespace detail

client::implementation::implementation(io_context& owner) : context(owner) {}

void client::implementation::close() noexcept
{
    connected = false;
    compression_active = false;
    querying = false;
    cancel_requested = false;
#ifdef CNETMOD_HAS_SSL
    tls_stream.reset();
    tls_context.reset();
#endif
    transport.close();
    incoming.clear();
}

void client::implementation::check_cancel(cancel_token& token)
{
    if (token.is_cancelled())
        throw detail::failure{transport_error(std::make_error_code(
            token.reason() == cancellation_reason::deadline_exceeded ? std::errc::timed_out : std::errc::operation_canceled))};
}

auto client::implementation::write(std::span<const std::byte> bytes, cancel_token& token) -> task<void>
{
    std::size_t offset{};
    while (offset < bytes.size())
    {
        check_cancel(token);
        std::expected<std::size_t, std::error_code> count;
#ifdef CNETMOD_HAS_SSL
        if (tls_stream)
            count = co_await with_deadline(context, expires,
                tls_stream->async_write(const_buffer{bytes.data() + offset, bytes.size() - offset}, token), token);
        else
#endif
            count = co_await with_deadline(context, expires,
                async_write(context, transport, const_buffer{bytes.data() + offset, bytes.size() - offset}, token), token);
        check_cancel(token);
        if (!count || !*count)
            throw detail::failure{transport_error(count ? std::make_error_code(std::errc::connection_reset) : count.error())};
        offset += *count;
    }
}

auto client::implementation::receive(cancel_token& token) -> task<packet>
{
    for (;;)
    {
        check_cancel(token);
        if (cancel_requested)
            throw detail::failure{error{.kind = error_kind::cancelled, .cause = std::make_error_code(std::errc::operation_canceled), .message = "ClickHouse query cancelled"}};
        try
        {
            detail::reader input{incoming, options.bounds};
            input.revision = revision;
            auto result = parse_packet(input, compression_active);
            incoming.erase(incoming.begin(), incoming.begin() + static_cast<std::ptrdiff_t>(input.position));
            if (result.kind == server_packet::exception)
                throw detail::failure{std::move(result.failure)};
            co_return result;
        }
        catch (const detail::failure& failure)
        {
            if (failure.value.cause != std::errc::result_out_of_range)
                throw;
        }
        if (incoming.size() >= options.bounds.max_packet_bytes)
            detail::fail("Native packet exceeds byte limit");
        std::array<std::byte, 16384> bytes;
        const auto available = std::min(bytes.size(), options.bounds.max_packet_bytes - incoming.size());
        std::expected<std::size_t, std::error_code> count;
#ifdef CNETMOD_HAS_SSL
        if (tls_stream)
            count = co_await with_deadline(context, expires,
                tls_stream->async_read(mutable_buffer{bytes.data(), available}, token), token);
        else
#endif
            count = co_await with_deadline(context, expires,
                async_read(context, transport, mutable_buffer{bytes.data(), available}, token), token);
        check_cancel(token);
        if (!count || !*count)
            throw detail::failure{transport_error(count ? std::make_error_code(std::errc::connection_reset) : count.error())};
        incoming.insert(incoming.end(), bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(*count));
    }
}
} // namespace cnetmod::clickhouse
