module;
#include <cnetmod/config.hpp>
module cnetmod.protocol.kafka.broker_connection;
import std;
import cnetmod.core.buffer;
import cnetmod.core.socket;
import cnetmod.core.dns;
import cnetmod.executor.async_op;
import cnetmod.coro.mutex;
import cnetmod.protocol.kafka.sasl_authenticator;
#ifdef CNETMOD_HAS_SSL
import cnetmod.core.ssl;
#endif
namespace cnetmod::kafka {
class broker_connection::impl
{
public:
    impl(io_context& c, broker_endpoint ep, client_options o)
        : ctx(c), remote(std::move(ep)), options(std::move(o)) {}

    auto open(cancel_token* token) -> task<result<void>>
    {
        close();
        if (token && token->is_cancelled())
            co_return std::unexpected(
                make_error(error_code::cancelled, "connection cancelled"));
        auto connected =
            co_await async_connect_happy_eyeballs(ctx, remote.host, remote.port);
        if (!connected)
            co_return std::unexpected(
                make_error(connected.error() ==
                            std::make_error_code(std::errc::operation_canceled)
                        ? error_code::cancelled
                        : error_code::transport,
                    connected.error().message()));
        sock = std::move(connected->sock);
#ifdef CNETMOD_HAS_SSL
        auto tls = endpoint_options().tls;
        if (tls.enabled)
        {
            auto c = ssl_context::client();
            if (!c)
            {
                close();
                co_return std::unexpected(
                    make_error(error_code::transport, c.error().message()));
            }
            ssl_ctx = std::make_unique<ssl_context>(std::move(*c));
            ssl_ctx->set_verify_peer(tls.verify_peer);
            if (!tls.ca_file.empty())
            {
                auto x = ssl_ctx->load_ca_file(tls.ca_file);
                if (!x)
                {
                    close();
                    co_return std::unexpected(
                        make_error(error_code::transport, x.error().message()));
                }
            }
            else if (tls.verify_peer)
                (void)ssl_ctx->set_default_ca();
            if (!tls.certificate_file.empty())
            {
                auto x = ssl_ctx->load_cert_file(tls.certificate_file);
                if (!x)
                {
                    close();
                    co_return std::unexpected(
                        make_error(error_code::transport, x.error().message()));
                }
            }
            if (!tls.private_key_file.empty())
            {
                auto x = ssl_ctx->load_key_file(tls.private_key_file);
                if (!x)
                {
                    close();
                    co_return std::unexpected(
                        make_error(error_code::transport, x.error().message()));
                }
            }
            ssl = std::make_unique<ssl_stream>(*ssl_ctx, ctx, sock);
            ssl->set_connect_state();
            ssl->set_hostname(tls.server_name.empty() ? remote.host
                                                      : tls.server_name);
            auto h = co_await ssl->async_handshake();
            if (!h)
            {
                close();
                co_return std::unexpected(
                    make_error(error_code::transport, h.error().message()));
            }
        }
#else
        if (endpoint_options().tls.enabled)
        {
            close();
            co_return std::unexpected(make_error(error_code::configuration,
                "TLS support is not available"));
        }
#endif
        if (options.sasl != sasl_mechanism::none)
        {
            std::unique_ptr<sasl_authenticator> auth;
            if (options.sasl == sasl_mechanism::plain)
                auth = make_plain_authenticator(options.credentials.username,
                    options.credentials.password);
            else
            {
                auto created = make_scram_authenticator(
                    options.sasl, options.credentials.username,
                    options.credentials.password, options.scram_crypto);
                if (!created)
                {
                    close();
                    co_return std::unexpected(created.error());
                }
                auth = std::move(*created);
            }
            protocol::encoder hs;
            hs.string(auth->mechanism_name());
            auto h = co_await exchange(protocol::api_key::sasl_handshake, 1,
                std::move(hs).take(), token);
            if (!h)
            {
                close();
                co_return std::unexpected(h.error());
            }
            protocol::decoder hd(*h);
            auto he = hd.int16();
            if (!he || *he != 0)
            {
                close();
                co_return std::unexpected(make_error(
                    he ? static_cast<error_code>(*he) : error_code::malformed_response,
                    "SASL handshake failed"));
            }
            auto count = hd.int32();
            if (!count || *count < 0)
            {
                close();
                co_return std::unexpected(make_error(error_code::malformed_response,
                    "invalid SASL mechanisms"));
            }
            for (int i = 0; i < *count; ++i)
            {
                auto ignored = hd.string();
                if (!ignored)
                {
                    close();
                    co_return std::unexpected(ignored.error());
                }
            }
            auto initial = auth->initial_response();
            if (!initial)
            {
                close();
                co_return std::unexpected(initial.error());
            }
            while (true)
            {
                protocol::encoder ae;
                ae.int32(static_cast<std::int32_t>(initial->size()));
                ae.raw(*initial);
                auto ar = co_await exchange(protocol::api_key::sasl_authenticate, 1,
                    std::move(ae).take(), token);
                if (!ar)
                {
                    close();
                    co_return std::unexpected(ar.error());
                }
                protocol::decoder ad(*ar);
                auto ec = ad.int16();
                auto message = ad.nullable_string();
                auto challenge = ad.byte_array();
                auto lifetime = ad.int64();
                if (!ec || !message || !challenge || !lifetime || *ec != 0)
                {
                    close();
                    co_return std::unexpected(make_error(
                        ec ? static_cast<error_code>(*ec)
                           : error_code::malformed_response,
                        message && *message ? **message : "SASL authentication failed"));
                }
                if (auth->complete())
                    break;
                auto next = auth->challenge(
                    challenge && *challenge ? std::span<const std::byte>(**challenge)
                                            : std::span<const std::byte>{});
                if (!next)
                {
                    close();
                    co_return std::unexpected(next.error());
                }
                initial = std::move(next);
            }
        }
        notify_connected();
        co_return result<void>{};
    }

    auto exchange(protocol::api_key key, std::int16_t version,
        std::span<const std::byte> body, cancel_token* token)
        -> task<result<bytes>>
    {
        if (!sock.is_open())
        {
            co_await connect_mutex.lock();
            async_lock_guard connect_guard(connect_mutex, std::adopt_lock);
            if (!sock.is_open())
            {
                auto c = co_await open(token);
                if (!c)
                    co_return std::unexpected(c.error());
            }
        }
        co_await request_mutex.lock();
        async_lock_guard request_guard(request_mutex, std::adopt_lock);
        auto id = next_correlation++;
        auto packet =
            protocol::encode_request({key, version, id, options.client_id}, body);
        auto w = co_await write_all({packet.data(), packet.size()});
        if (!w)
        {
            auto e = make_error(error_code::transport, w.error().message());
            notify_disconnected(e);
            close();
            co_return std::unexpected(std::move(e));
        }
        std::array<std::byte, 4> prefix{};
        auto r = co_await read_exact(prefix);
        if (!r)
        {
            auto e = make_error(error_code::transport, r.error().message());
            notify_disconnected(e);
            close();
            co_return std::unexpected(std::move(e));
        }
        protocol::decoder pd(prefix);
        auto n = pd.int32();
        if (!n || *n < 4 ||
            static_cast<std::size_t>(*n) > options.max_response_bytes)
        {
            close();
            co_return std::unexpected(make_error(error_code::malformed_response,
                "invalid Kafka response length"));
        }
        bytes response(static_cast<std::size_t>(*n));
        auto rr = co_await read_exact(response);
        if (!rr)
        {
            close();
            co_return std::unexpected(
                make_error(error_code::transport, rr.error().message()));
        }
        protocol::decoder d(response);
        auto h = protocol::decode_response_header(d);
        if (!h || h->correlation_id != id)
        {
            close();
            co_return std::unexpected(make_error(error_code::malformed_response,
                "Kafka correlation id mismatch"));
        }
        auto payload = d.slice(d.remaining());
        co_return bytes(payload->begin(), payload->end());
    }

    auto send_only(protocol::api_key key, std::int16_t version,
        std::span<const std::byte> body, cancel_token* token)
        -> task<result<void>>
    {
        if (!sock.is_open())
        {
            co_await connect_mutex.lock();
            async_lock_guard connect_guard(connect_mutex, std::adopt_lock);
            if (!sock.is_open())
            {
                auto connected = co_await open(token);
                if (!connected)
                    co_return std::unexpected(connected.error());
            }
        }
        co_await request_mutex.lock();
        async_lock_guard request_guard(request_mutex, std::adopt_lock);
        auto packet = protocol::encode_request(
            {key, version, next_correlation++, options.client_id}, body);
        auto written = co_await write_all({packet.data(), packet.size()});
        if (!written)
        {
            auto failure =
                make_error(error_code::transport, written.error().message());
            notify_disconnected(failure);
            close();
            co_return std::unexpected(std::move(failure));
        }
        co_return result<void>{};
    }

    auto endpoint_options() const -> const client_endpoint&
    {
        for (auto& e : options.bootstrap_servers)
            if (e.host == remote.host && e.port == remote.port)
                return e;
        static const client_endpoint plain{};
        return plain;
    }

    auto write_all(const_buffer b) -> task<std::expected<void, std::error_code>>
    {
#ifdef CNETMOD_HAS_SSL
        if (ssl)
            co_return co_await ssl->async_write_all(b);
#endif
        co_return co_await async_write_all(ctx, sock, b);
    }

    auto read_some(mutable_buffer b)
        -> task<std::expected<std::size_t, std::error_code>>
    {
#ifdef CNETMOD_HAS_SSL
        if (ssl)
            co_return co_await ssl->async_read(b);
#endif
        co_return co_await async_read(ctx, sock, b);
    }

    auto read_exact(std::span<std::byte> b)
        -> task<std::expected<void, std::error_code>>
    {
        std::size_t at = 0;
        while (at < b.size())
        {
            auto r = co_await read_some({b.data() + at, b.size() - at});
            if (!r)
                co_return std::unexpected(r.error());
            if (*r == 0)
                co_return std::unexpected(
                    std::make_error_code(std::errc::connection_reset));
            at += *r;
        }
        co_return std::expected<void, std::error_code>{};
    }

    void close() noexcept
    {
#ifdef CNETMOD_HAS_SSL
        ssl.reset();
        ssl_ctx.reset();
#endif
        sock.close();
    }

    void notify_connected()
    {
        visit([&](connection_observer& o)
            {
                o.on_connected(remote);
            });
    }

    void notify_disconnected(const error& e)
    {
        visit([&](connection_observer& o)
            {
                o.on_disconnected(remote, e);
            });
    }

    template <class F> void visit(F f)
    {
        std::erase_if(observers, [&](auto& w)
            {
                if (auto x = w.lock())
                {
                    f(*x);
                    return false;
                }
                return true;
            });
    }

    io_context& ctx;
    broker_endpoint remote;
    client_options options;
    socket sock;
    std::int32_t next_correlation = 1;
    std::vector<std::weak_ptr<connection_observer>> observers;
    async_mutex connect_mutex;
    async_mutex request_mutex;
#ifdef CNETMOD_HAS_SSL
    std::unique_ptr<ssl_context> ssl_ctx;
    std::unique_ptr<ssl_stream> ssl;
#endif
};

broker_connection::broker_connection(io_context& c, broker_endpoint e,
    client_options o)
    : impl_(std::make_unique<impl>(c, std::move(e), std::move(o))) {}

broker_connection::~broker_connection() = default;
broker_connection::broker_connection(broker_connection&&) noexcept = default;
auto broker_connection::operator=(broker_connection&&) noexcept
    -> broker_connection& = default;

auto broker_connection::connect() -> task<result<void>>
{
    co_return co_await impl_->open(nullptr);
}

auto broker_connection::connect(cancel_token& t) -> task<result<void>>
{
    co_return co_await impl_->open(&t);
}

auto broker_connection::request(protocol::api_key k, std::int16_t v,
    std::span<const std::byte> b)
    -> task<result<bytes>>
{
    co_return co_await impl_->exchange(k, v, b, nullptr);
}

auto broker_connection::request(protocol::api_key k, std::int16_t v,
    std::span<const std::byte> b, cancel_token& t)
    -> task<result<bytes>>
{
    co_return co_await impl_->exchange(k, v, b, &t);
}

auto broker_connection::send(protocol::api_key k, std::int16_t v,
    std::span<const std::byte> b)
    -> task<result<void>>
{
    co_return co_await impl_->send_only(k, v, b, nullptr);
}

auto broker_connection::send(protocol::api_key k, std::int16_t v,
    std::span<const std::byte> b, cancel_token& t)
    -> task<result<void>>
{
    co_return co_await impl_->send_only(k, v, b, &t);
}

void broker_connection::close() noexcept
{
    impl_->close();
}

auto broker_connection::is_open() const noexcept -> bool
{
    return impl_->sock.is_open();
}

auto broker_connection::endpoint() const noexcept -> const broker_endpoint&
{
    return impl_->remote;
}

void broker_connection::add_observer(std::weak_ptr<connection_observer> x)
{
    impl_->observers.push_back(std::move(x));
}
} // namespace cnetmod::kafka
