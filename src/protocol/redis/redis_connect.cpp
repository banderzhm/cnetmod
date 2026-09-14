module cnetmod.protocol.redis;

import std;
import :client;
import :request;
import :value;
import cnetmod.core.dns;
import cnetmod.coro.cancel;
import cnetmod.coro.task;
#ifdef CNETMOD_HAS_SSL
import cnetmod.core.ssl;
#endif

namespace cnetmod::redis {

auto client::connect(connect_options opts, cancel_token& cancellation)
    -> task<std::expected<void, std::error_code>>
{
    if (cancellation.is_cancelled())
        co_return std::unexpected(std::make_error_code(
            cancellation.reason() == cancellation_reason::deadline_exceeded
                ? std::errc::timed_out
                : std::errc::operation_canceled));
    close();

    /**
     * @brief Publishes a session only after all configured handshakes succeed.
     */
    struct connection_guard
    {
        client& owner;
        bool committed = false;

        ~connection_guard()
        {
            if (!committed)
                owner.close();
        }
    } guard{*this};

    opts_ = opts;
    auto connected = co_await async_connect_happy_eyeballs(
        ctx_, opts.host, opts.port, {}, cancellation);
    if (!connected)
        co_return std::unexpected(connected.error());
    sock_ = std::move(connected->sock);
#ifdef CNETMOD_HAS_SSL
    if (opts.tls)
    {
        auto configured = configure_tls(opts, true);
        if (!configured)
            co_return std::unexpected(configured.error().code);
        auto handshake = co_await ssl_->async_handshake(cancellation);
        if (!handshake)
            co_return std::unexpected(handshake.error());
    }
#else
    if (opts.tls)
        co_return std::unexpected(std::make_error_code(std::errc::not_supported));
#endif
    if (opts.resp3)
    {
        request hello;
        if (!opts.password.empty())
            hello.push("HELLO", "3", "AUTH",
                opts.username.empty() ? "default" : opts.username, opts.password);
        else
            hello.push("HELLO", "3");
        auto reply = co_await exchange(hello, cancellation);
        if (!reply)
            co_return std::unexpected(reply.error());
        resp3_mode_ = !has_error(*reply);
    }
    if (!resp3_mode_ && !opts.password.empty())
    {
        request auth;
        if (opts.username.empty())
            auth.push("AUTH", opts.password);
        else
            auth.push("AUTH", opts.username, opts.password);
        auto reply = co_await exchange(auth, cancellation);
        if (!reply)
            co_return std::unexpected(reply.error());
        if (has_error(*reply))
            co_return std::unexpected(make_error_code(redis_errc::resp3_simple_error));
        if (!is_ok(*reply))
            co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
    }
    if (opts.db > 0U)
    {
        request select;
        select.push("SELECT", std::to_string(opts.db));
        auto reply = co_await exchange(select, cancellation);
        if (!reply)
            co_return std::unexpected(reply.error());
        if (has_error(*reply))
            co_return std::unexpected(make_error_code(redis_errc::resp3_simple_error));
        if (!is_ok(*reply))
            co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
    }
    guard.committed = true;
    co_return {};
}

} // namespace cnetmod::redis
