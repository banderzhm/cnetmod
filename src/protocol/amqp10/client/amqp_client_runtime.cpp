module;
#include <cnetmod/config.hpp>
module cnetmod.protocol.amqp10;
import :amqp_client;
import std;
import cnetmod.coro.mutex;
import cnetmod.coro.spawn;
import cnetmod.coro.timer;
import cnetmod.executor.async_op;
import :socket_transport;
import :transport_frame_codec;
import :performative_codec;
import :sasl_negotiator;
import :protocol_error;

namespace cnetmod::amqp10 {
struct client::impl
{
    io_context& ctx;
    std::unique_ptr<socket_transport> transport;
    client_options options;
    connection_state current = connection_state::idle;
    state_handler state_callback;
    disconnect_handler disconnect_callback;
    std::map<symbol, value, std::less<>> peer_properties;
    std::uint32_t peer_max_frame = 262144;
    std::uint16_t peer_channel_max = 65535;
    std::uint16_t next_channel = 1;
    async_mutex write_mutex;
    async_mutex read_mutex;
    std::mutex pending_mutex;
    std::map<std::uint16_t, std::deque<performative>> pending;
    std::shared_ptr<cancel_token> heartbeat_cancel;
    std::shared_ptr<cancel_token> pump_cancel;
    std::shared_ptr<cancel_token> reconnect_cancel;
    std::chrono::milliseconds remote_idle_timeout{};
    std::atomic<std::int64_t> last_received_ticks{0};
    bool pump_started = false;
    bool reconnecting = false;
    std::vector<recovery_observer*> recovery_observers;

    explicit impl(io_context& c)
        : ctx(c), transport(std::make_unique<socket_transport>(c)) {}

    void transition(connection_state next)
    {
        current = next;
        if (state_callback)
            state_callback(next);
    }
};

client::client(io_context& c)
    : impl_(std::make_unique<impl>(c)) {}

client::~client()
{
    if (impl_ && impl_->transport)
        impl_->transport->close();
}

client::client(client&&) noexcept = default;
auto client::operator=(client&&) noexcept -> client& = default;

namespace {
    auto expect_header(socket_transport& t, protocol_header sent,
        cancel_token& token)
        -> task<std::expected<void, error>>
    {
        auto w = co_await t.write_header(sent, token);
        if (!w)
            co_return std::unexpected(w.error());
        auto h = co_await t.read_header(token);
        if (!h)
            co_return std::unexpected(h.error());
        if (h->protocol != sent.protocol || h->major != 1 || h->minor != 0 ||
            h->revision != 0)
            co_return std::unexpected(
                make_error(error_stage::handshake, errc::protocol_state,
                    "peer rejected AMQP 1.0 protocol header"));
        co_return {};
    }

    auto read_typed(socket_transport& t, frame_type type, std::uint32_t maximum,
        cancel_token& token)
        -> task<std::expected<frame, error>>
    {
        auto f = co_await t.read_frame(maximum, token);
        if (!f)
            co_return std::unexpected(f.error());
        if (f->type != type)
            co_return std::unexpected(make_error(error_stage::protocol,
                errc::unexpected_performative,
                "unexpected AMQP frame type"));
        co_return std::move(*f);
    }
} // namespace

auto client::connect(client_options options, cancel_token& token)
    -> task<std::expected<void, error>>
{
    if (impl_->current != connection_state::idle &&
        impl_->current != connection_state::closed &&
        impl_->current != connection_state::failed)
        co_return std::unexpected(make_error(error_stage::configuration,
            errc::protocol_state,
            "AMQP client is already active"));
    if (options.endpoint.port == 0)
        options.endpoint.port = options.endpoint.tls.enabled ? 5671 : 5672;
    if (options.container_id.empty())
        options.container_id =
            "cnetmod-" +
            std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count());
    impl_->options = std::move(options);
    impl_->transition(connection_state::connecting);
    auto connected =
        co_await impl_->transport->connect(impl_->options.endpoint, token);
    if (!connected)
    {
        impl_->transition(connection_state::failed);
        co_return std::unexpected(connected.error());
    }
    impl_->transition(connection_state::sasl);
    auto sh = co_await expect_header(*impl_->transport,
        protocol_header{protocol_id::sasl}, token);
    if (!sh)
    {
        impl_->transition(connection_state::failed);
        co_return std::unexpected(sh.error());
    }
    auto mechanisms_frame =
        co_await read_typed(*impl_->transport, frame_type::sasl,
            impl_->options.max_frame_size, token);
    if (!mechanisms_frame)
    {
        impl_->transition(connection_state::failed);
        co_return std::unexpected(mechanisms_frame.error());
    }
    auto mechanisms_value = decode_sasl_performative(mechanisms_frame->body);
    if (!mechanisms_value ||
        !std::holds_alternative<sasl_mechanisms>(*mechanisms_value))
    {
        impl_->transition(connection_state::failed);
        co_return std::unexpected(make_error(error_stage::authentication,
            errc::unexpected_performative,
            "expected SASL mechanisms"));
    }
    sasl_negotiator negotiator(impl_->options.credentials);
    auto init = negotiator.select(
        std::get<sasl_mechanisms>(*mechanisms_value).mechanisms,
        impl_->options.hostname.empty() ? impl_->options.endpoint.host
                                        : impl_->options.hostname);
    if (!init)
    {
        impl_->transition(connection_state::failed);
        co_return std::unexpected(init.error());
    }
    auto init_body = encode_sasl_performative(*init);
    auto init_write = co_await impl_->transport->write_frame(
        frame{frame_type::sasl, 0, std::move(init_body)}, token);
    if (!init_write)
    {
        impl_->transition(connection_state::failed);
        co_return std::unexpected(init_write.error());
    }
    while (true)
    {
        auto reply = co_await read_typed(*impl_->transport, frame_type::sasl,
            impl_->options.max_frame_size, token);
        if (!reply)
        {
            impl_->transition(connection_state::failed);
            co_return std::unexpected(reply.error());
        }
        auto decoded = decode_sasl_performative(reply->body);
        if (!decoded)
        {
            impl_->transition(connection_state::failed);
            co_return std::unexpected(
                error{.stage = error_stage::authentication,
                    .code = decoded.error(),
                    .message = "invalid SASL frame"});
        }
        if (auto challenge = std::get_if<sasl_challenge>(&*decoded))
        {
            auto response = negotiator.respond(challenge->challenge);
            if (!response)
            {
                impl_->transition(connection_state::failed);
                co_return std::unexpected(response.error());
            }
            auto body = encode_sasl_performative(*response);
            auto wr = co_await impl_->transport->write_frame(
                frame{frame_type::sasl, 0, std::move(body)}, token);
            if (!wr)
            {
                impl_->transition(connection_state::failed);
                co_return std::unexpected(wr.error());
            }
            continue;
        }
        if (auto outcome = std::get_if<sasl_outcome>(&*decoded))
        {
            auto done = negotiator.finish(*outcome);
            if (!done)
            {
                impl_->transition(connection_state::failed);
                co_return std::unexpected(done.error());
            }
            break;
        }
        impl_->transition(connection_state::failed);
        co_return std::unexpected(make_error(error_stage::authentication,
            errc::unexpected_performative,
            "unexpected SASL response"));
    }
    impl_->transition(connection_state::opening);
    auto ah = co_await expect_header(*impl_->transport,
        protocol_header{protocol_id::amqp}, token);
    if (!ah)
    {
        impl_->transition(connection_state::failed);
        co_return std::unexpected(ah.error());
    }
    open local{.container_id = impl_->options.container_id,
        .hostname = impl_->options.hostname,
        .max_frame_size = impl_->options.max_frame_size,
        .channel_max = impl_->options.channel_max,
        .idle_timeout = impl_->options.idle_timeout};
    auto sent = co_await send(0, performative{local}, token);
    if (!sent)
    {
        impl_->transition(connection_state::failed);
        co_return std::unexpected(sent.error());
    }
    auto peer = co_await receive(0, token);
    if (!peer || !std::holds_alternative<open>(*peer))
    {
        impl_->transition(connection_state::failed);
        co_return std::unexpected(
            peer ? make_error(error_stage::handshake,
                       errc::unexpected_performative, "expected AMQP Open")
                 : peer.error());
    }
    const auto& remote = std::get<open>(*peer);
    impl_->peer_max_frame = std::max<std::uint32_t>(512, remote.max_frame_size);
    impl_->peer_channel_max = remote.channel_max;
    impl_->peer_properties = remote.properties;
    impl_->remote_idle_timeout = remote.idle_timeout;
    impl_->last_received_ticks.store(
        std::chrono::steady_clock::now().time_since_epoch().count(),
        std::memory_order_relaxed);
    impl_->transition(connection_state::opened);
    impl_->pump_cancel = std::make_shared<cancel_token>();
    impl_->pump_started = true;
    spawn(impl_->ctx, read_pump(impl_->pump_cancel));
    if (impl_->options.idle_timeout > std::chrono::milliseconds::zero() ||
        impl_->remote_idle_timeout > std::chrono::milliseconds::zero())
    {
        impl_->heartbeat_cancel = std::make_shared<cancel_token>();
        spawn(impl_->ctx, heartbeat_loop(impl_->heartbeat_cancel));
    }
    co_return {};
}

auto client::reconnect(cancel_token& token)
    -> task<std::expected<void, error>>
{
    auto saved = impl_->options;
    if (impl_->heartbeat_cancel)
        impl_->heartbeat_cancel->cancel();
    if (impl_->pump_cancel)
        impl_->pump_cancel->cancel();
    impl_->pump_started = false;
    impl_->transport->close();
    impl_->transport = std::make_unique<socket_transport>(impl_->ctx);
    impl_->current = connection_state::closed;
    {
        std::scoped_lock lock(impl_->pending_mutex);
        impl_->pending.clear();
    }
    reconnect_context context;
    while (true)
    {
        auto connected = co_await connect(saved, token);
        if (connected)
        {
            if (saved.recover_sessions)
            {
                auto observers = impl_->recovery_observers;
                std::ranges::sort(observers, {}, &recovery_observer::recovery_order);
                for (auto* observer : observers)
                {
                    if (!observer)
                        continue;
                    auto restored = co_await observer->recover(token);
                    if (!restored)
                        co_return std::unexpected(restored.error());
                }
            }
            co_return {};
        }
        if (!saved.reconnect)
            co_return std::unexpected(connected.error());
        auto delay = saved.reconnect->next_delay(context);
        if (!delay)
            co_return std::unexpected(connected.error());
        context.previous_delay = *delay;
        ++context.attempt;
        auto waited = co_await async_timer_wait(impl_->ctx, *delay, token);
        if (!waited)
            co_return std::unexpected(make_error(error_stage::cancelled,
                errc::cancelled,
                "AMQP reconnect cancelled"));
        impl_->transport = std::make_unique<socket_transport>(impl_->ctx);
        impl_->current = connection_state::closed;
    }
}

auto client::heartbeat_loop(std::shared_ptr<cancel_token> token) -> task<void>
{
    auto interval = std::chrono::milliseconds::max();
    if (impl_->remote_idle_timeout > std::chrono::milliseconds::zero())
        interval = std::min(interval, std::max(std::chrono::milliseconds{1}, impl_->remote_idle_timeout / 2));
    if (impl_->options.idle_timeout > std::chrono::milliseconds::zero())
        interval = std::min(interval, std::max(std::chrono::milliseconds{1}, impl_->options.idle_timeout / 2));
    while (!token->is_cancelled() && impl_->current == connection_state::opened)
    {
        auto waited = co_await async_timer_wait(impl_->ctx, interval, *token);
        if (!waited || token->is_cancelled())
            co_return;
        if (impl_->options.idle_timeout > std::chrono::milliseconds::zero())
        {
            const auto last = std::chrono::steady_clock::time_point(
                std::chrono::steady_clock::duration(
                    impl_->last_received_ticks.load(std::memory_order_relaxed)));
            if (std::chrono::steady_clock::now() - last >=
                impl_->options.idle_timeout)
            {
                auto failure =
                    make_error(error_stage::transport, errc::idle_timeout,
                        "peer exceeded negotiated AMQP idle-time-out", true);
                if (impl_->pump_cancel)
                    impl_->pump_cancel->cancel();
                impl_->transport->close();
                impl_->transition(connection_state::failed);
                if (impl_->disconnect_callback)
                    impl_->disconnect_callback(failure);
                if (impl_->options.reconnect && !impl_->reconnecting)
                {
                    impl_->reconnecting = true;
                    impl_->reconnect_cancel = std::make_shared<cancel_token>();
                    spawn(impl_->ctx, automatic_reconnect(impl_->reconnect_cancel));
                }
                co_return;
            }
        }
        if (impl_->remote_idle_timeout <= std::chrono::milliseconds::zero())
            continue;
        co_await impl_->write_mutex.lock();
        async_lock_guard guard(impl_->write_mutex, std::adopt_lock);
        auto sent = co_await impl_->transport->write_frame(
            frame{frame_type::amqp, 0, {}}, *token);
        if (!sent)
        {
            impl_->transition(connection_state::failed);
            if (impl_->disconnect_callback)
                impl_->disconnect_callback(sent.error());
            if (impl_->options.reconnect && !impl_->reconnecting)
            {
                impl_->reconnecting = true;
                impl_->reconnect_cancel = std::make_shared<cancel_token>();
                spawn(impl_->ctx, automatic_reconnect(impl_->reconnect_cancel));
            }
            co_return;
        }
    }
}

auto client::send(std::uint16_t channel, const performative& p,
    cancel_token& token)
    -> task<std::expected<void, error>>
{
    co_await impl_->write_mutex.lock();
    async_lock_guard guard(impl_->write_mutex, std::adopt_lock);
    if (token.is_cancelled())
        co_return std::unexpected(make_error(error_stage::cancelled,
            errc::cancelled,
            "AMQP write cancelled"));
    auto body = encode_performative(p);
    co_return co_await impl_->transport->write_frame(
        frame{frame_type::amqp, channel, std::move(body)}, token);
}

auto client::receive(std::uint16_t channel, cancel_token& token)
    -> task<std::expected<performative, error>>
{
    if (impl_->pump_started)
    {
        while (!token.is_cancelled())
        {
            {
                std::scoped_lock lock(impl_->pending_mutex);
                auto it = impl_->pending.find(channel);
                if (it != impl_->pending.end() && !it->second.empty())
                {
                    auto next = std::move(it->second.front());
                    it->second.pop_front();
                    co_return next;
                }
            }
            if (impl_->current == connection_state::failed ||
                impl_->current == connection_state::closed)
                co_return std::unexpected(make_error(
                    error_stage::transport, errc::connection_closed,
                    "AMQP receive pump stopped", true));
            auto waited = co_await async_timer_wait(
                impl_->ctx, std::chrono::milliseconds{2}, token);
            if (!waited)
                break;
        }
        co_return std::unexpected(make_error(error_stage::cancelled,
            errc::cancelled,
            "AMQP receive cancelled"));
    }

    co_await impl_->read_mutex.lock();
    async_lock_guard guard(impl_->read_mutex, std::adopt_lock);
    while (true)
    {
        auto incoming = co_await impl_->transport->read_frame(
            impl_->options.max_frame_size, token);
        if (!incoming)
        {
            impl_->transition(connection_state::failed);
            if (impl_->disconnect_callback)
                impl_->disconnect_callback(incoming.error());
            co_return std::unexpected(incoming.error());
        }
        if (incoming->body.empty())
            continue;
        auto decoded = decode_performative(incoming->body);
        if (!decoded)
            co_return std::unexpected(
                error{.stage = error_stage::protocol,
                    .code = decoded.error(),
                    .message = "cannot decode AMQP performative"});
        if (incoming->channel == channel)
            co_return std::move(*decoded);
        std::scoped_lock lock(impl_->pending_mutex);
        impl_->pending[incoming->channel].push_back(std::move(*decoded));
    }
}

auto client::read_pump(std::shared_ptr<cancel_token> token) -> task<void>
{
    while (!token->is_cancelled() && impl_->transport->is_open())
    {
        auto incoming = co_await impl_->transport->read_frame(
            impl_->options.max_frame_size, *token);
        if (!incoming)
        {
            if (!token->is_cancelled())
            {
                impl_->transition(connection_state::failed);
                if (impl_->disconnect_callback)
                    impl_->disconnect_callback(incoming.error());
                if (impl_->options.reconnect && !impl_->reconnecting)
                {
                    impl_->reconnecting = true;
                    impl_->reconnect_cancel = std::make_shared<cancel_token>();
                    spawn(impl_->ctx, automatic_reconnect(impl_->reconnect_cancel));
                }
            }
            co_return;
        }
        impl_->last_received_ticks.store(
            std::chrono::steady_clock::now().time_since_epoch().count(),
            std::memory_order_relaxed);
        if (incoming->body.empty())
            continue;
        auto decoded = decode_performative(incoming->body);
        if (!decoded)
        {
            auto failure =
                error{.stage = error_stage::protocol,
                    .code = decoded.error(),
                    .message = "cannot decode AMQP performative"};
            impl_->transition(connection_state::failed);
            if (impl_->disconnect_callback)
                impl_->disconnect_callback(failure);
            impl_->transport->close();
            co_return;
        }
        if (std::holds_alternative<cnetmod::amqp10::close>(*decoded))
        {
            auto closed = make_error(error_stage::protocol,
                errc::connection_closed,
                "peer closed the AMQP connection");
            impl_->transition(connection_state::closed);
            if (impl_->disconnect_callback)
                impl_->disconnect_callback(closed);
            impl_->transport->close();
            co_return;
        }
        std::scoped_lock lock(impl_->pending_mutex);
        impl_->pending[incoming->channel].push_back(std::move(*decoded));
    }
}

auto client::automatic_reconnect(std::shared_ptr<cancel_token> token)
    -> task<void>
{
    auto restored = co_await reconnect(*token);
    impl_->reconnecting = false;
    if (!restored && impl_->disconnect_callback)
        impl_->disconnect_callback(restored.error());
}

auto client::maximum_frame_size() const noexcept -> std::uint32_t
{
    return std::min(impl_->options.max_frame_size, impl_->peer_max_frame);
}

void client::register_recovery_observer(recovery_observer& observer)
{
    if (std::ranges::find(impl_->recovery_observers, &observer) ==
        impl_->recovery_observers.end())
        impl_->recovery_observers.push_back(&observer);
}

void client::unregister_recovery_observer(
    recovery_observer& observer) noexcept
{
    std::erase(impl_->recovery_observers, &observer);
}

auto client::make_session(session_options options)
    -> std::expected<session, error>
{
    if (impl_->current != connection_state::opened)
        return std::unexpected(make_error(error_stage::protocol,
            errc::protocol_state,
            "AMQP connection is not open"));
    if (impl_->next_channel >
        std::min(impl_->options.channel_max, impl_->peer_channel_max))
        return std::unexpected(make_error(error_stage::flow_control,
            errc::protocol_state,
            "AMQP channel maximum reached"));
    return session::create(*this, impl_->next_channel++, options);
}

auto client::close(cancel_token& token)
    -> task<std::expected<void, error>>
{
    if (impl_->current == connection_state::closed ||
        impl_->current == connection_state::idle)
        co_return {};
    if (impl_->heartbeat_cancel)
        impl_->heartbeat_cancel->cancel();
    impl_->transition(connection_state::closing);
    auto sent = co_await send(
        0, performative{cnetmod::amqp10::close{}}, token);
    if (!sent)
    {
        if (impl_->pump_cancel)
            impl_->pump_cancel->cancel();
        impl_->transport->close();
        impl_->pump_started = false;
        impl_->transition(connection_state::closed);
        co_return std::unexpected(sent.error());
    }
    auto peer = co_await receive(0, token);
    if (peer && !std::holds_alternative<cnetmod::amqp10::close>(*peer))
        co_return std::unexpected(make_error(error_stage::protocol,
            errc::unexpected_performative,
            "expected AMQP Close"));
    if (impl_->pump_cancel)
        impl_->pump_cancel->cancel();
    impl_->pump_started = false;
    co_await impl_->transport->shutdown();
    impl_->transition(connection_state::closed);
    co_return {};
}

void client::on_state_change(state_handler h)
{
    impl_->state_callback = std::move(h);
}

void client::on_disconnect(disconnect_handler h)
{
    impl_->disconnect_callback = std::move(h);
}

auto client::state() const noexcept -> connection_state
{
    return impl_->current;
}

auto client::remote_properties() const -> std::map<symbol, value, std::less<>>
{
    return impl_->peer_properties;
}
} // namespace cnetmod::amqp10
