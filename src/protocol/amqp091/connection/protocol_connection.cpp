module;
#include <cnetmod/config.hpp>
module cnetmod.protocol.amqp091;
import :protocol_connection;
import std;
import cnetmod.core.buffer;
import cnetmod.core.socket;
import cnetmod.core.dns;
import cnetmod.core.error;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.cancel;
import cnetmod.coro.mutex;
import cnetmod.coro.wait_group;
import cnetmod.coro.spawn;
import cnetmod.coro.timer;
import cnetmod.executor.async_op;
import cnetmod.utils.concurrent_containers.atomic_rw_latch;
#ifdef CNETMOD_HAS_SSL
import cnetmod.core.ssl;
#endif
import :protocol_constants;
import :connection_options;
import :wire_frame_codec;
import :field_table_codec;
import :message_delivery;
import :publisher_confirm;
import :topology_recovery;
import :logical_channel;

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

        void long_string(std::string_view v)
        {
            integer(static_cast<std::uint32_t>(v.size()));
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

        auto long_string() -> std::optional<std::string>
        {
            auto n = integer<std::uint32_t>();
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

    auto transport_error(std::string where, const std::error_code& ec) -> error
    {
        if (ec == std::errc::not_enough_memory)
            return make_error(error_code::not_enough_memory, "transport allocation failed");
        return make_error(error_code::connection_closed,
            std::move(where) + ": " + ec.message(), true);
    }

    auto reply_error(std::uint16_t code, std::string text, std::uint16_t cls,
        std::uint16_t method) -> error
    {
        error_code mapped = error_code::command_invalid;
        switch (code)
        {
        case 403:
            mapped = error_code::access_refused;
            break;
        case 404:
            mapped = error_code::not_found;
            break;
        case 405:
            mapped = error_code::resource_locked;
            break;
        case 406:
            mapped = error_code::precondition_failed;
            break;
        default:
            break;
        }
        auto value = make_error(mapped, std::move(text), code >= 500);
        value.reply_code = code;
        value.class_id = cls;
        value.method_id = method;
        return value;
    }

    struct pending_rpc
    {
        std::uint16_t expected_class = 0, expected_method = 0;
        std::optional<result<method_frame>> outcome;
        std::coroutine_handle<> waiter{};
    };

    struct rpc_awaiter
    {
        std::shared_ptr<pending_rpc> value;

        auto await_ready() const noexcept -> bool
        {
            return value->outcome.has_value();
        }

        void await_suspend(std::coroutine_handle<> h) noexcept
        {
            value->waiter = h;
        }

        auto await_resume() -> result<method_frame>
        {
            return std::move(value->outcome.value());
        }
    };

    struct reader_claim_guard
    {
        std::atomic_bool& claimed;

        ~reader_claim_guard()
        {
            claimed.store(false, std::memory_order_release);
        }
    };

    struct inbound_content
    {
        enum class kind
        {
            delivery,
            returned
        } type = kind::delivery;
        delivery delivered;
        returned_message returned;
        std::uint64_t expected = 0;
    };
} // namespace

struct protocol_connection::impl
{
    explicit impl(io_context& c)
        : ctx(c) {}

    io_context& ctx;
    socket sock;
    connection_options options;
    std::atomic<connection_state> current{connection_state::disconnected};
    frame_parser parser;
    std::deque<frame> ready;
    async_mutex write_mutex;
#ifdef CNETMOD_HAS_SSL
    std::unique_ptr<ssl_context> ssl_ctx;
    std::unique_ptr<ssl_stream> ssl;
#endif
    std::uint16_t channel_max = 0, next_channel = 1;
    std::uint32_t frame_max = 131072;
    std::chrono::seconds heartbeat{0};
    // Reader, RPC registration and recovery all touch the same connection
    // bookkeeping. Keep each map transaction atomic without a platform
    // mutex; no coroutine suspension is permitted while this latch is held.
    concurrent_containers::atomic_rw_latch state_latch;
    std::atomic<std::int64_t> last_received_ns{0};
    std::atomic_bool reader_claimed{false};
    bool transport_interrupted = false;
    bool recovery_active = false;
    cancel_token* recovery_cancellation = nullptr;
    async_wait_group recovery_completed;
    std::uint64_t generation = 0;
    std::shared_ptr<cancel_token> managed_reader_token;

    struct reader_completion
    {
        async_wait_group started;
        async_wait_group completed;
        std::exception_ptr failure;
        result<void> outcome;
        bool start_notified = false;

        void notify_started() noexcept
        {
            if (!std::exchange(start_notified, true))
                started.done();
        }
    };

    std::shared_ptr<reader_completion> managed_reader_completion;
    std::map<std::uint16_t, std::shared_ptr<pending_rpc>> pending;
    std::map<std::uint16_t, std::map<std::string, delivery_handler, std::less<>>>
        delivery_handlers;
    std::map<std::uint16_t, std::shared_ptr<publisher_confirm_tracker>> confirms;
    std::map<std::uint16_t, inbound_content> content;
    std::vector<std::weak_ptr<connection_observer>> observers;
    return_handler returned;
    std::shared_ptr<topology_recorder> topology_state =
        std::make_shared<topology_recorder>();
    std::shared_ptr<recovery_strategy> recovery;

    /**
     * @brief Interrupts I/O without destroying state borrowed by active operations.
     */
    void interrupt_transport() noexcept
    {
        transport_interrupted = true;
        sock.shutdown_both();
    }

    /**
     * @brief Releases transport storage after all borrowing I/O has finished.
     * The caller owns the write lock and excludes an active reader.
     */
    void release_transport() noexcept
    {
#ifdef CNETMOD_HAS_SSL
        ssl.reset();
        ssl_ctx.reset();
#endif
        sock.close();
    }

    /**
     * @brief Completes abandoned RPCs without allocating a temporary collection.
     *
     * Remove each entry before resuming its caller and never resume under the
     * state latch. Empty diagnostic text keeps the teardown path allocation-free.
     */
    void fail_pending_rpcs() noexcept
    {
        for (;;)
        {
            std::shared_ptr<pending_rpc> call;
            {
                concurrent_containers::exclusive_latch_guard lock{state_latch};
                if (pending.empty())
                    return;
                auto first = pending.begin();
                call = std::move(first->second);
                pending.erase(first);
            }
            call->outcome = std::unexpected(error{.code = error_code::connection_closed});
            const auto waiter = std::exchange(call->waiter, {});
            if (waiter)
                waiter.resume();
        }
    }

    /**
     * @brief Retires confirmation trackers before notifying disconnected publishers.
     */
    void fail_pending_confirmations() noexcept
    {
        const error reason{.code = error_code::connection_closed};
        for (;;)
        {
            std::shared_ptr<publisher_confirm_tracker> tracker;
            {
                concurrent_containers::exclusive_latch_guard lock{state_latch};
                if (confirms.empty())
                    return;
                auto first = confirms.begin();
                tracker = std::move(first->second);
                confirms.erase(first);
            }
            tracker->fail_all(reason);
        }
    }

    void transition(connection_state value)
    {
        current.store(value);
        std::vector<std::shared_ptr<connection_observer>> listeners;
        {
            concurrent_containers::exclusive_latch_guard lock{state_latch};
            for (auto it = observers.begin(); it != observers.end();)
                if (auto p = it->lock())
                {
                    listeners.push_back(std::move(p));
                    ++it;
                }
                else
                    it = observers.erase(it);
        }
        for (auto& x : listeners)
            x->on_state_changed(value);
    }

    void notify(error reason)
    {
        std::vector<std::shared_ptr<connection_observer>> listeners;
        {
            concurrent_containers::shared_latch_guard lock{state_latch};
            for (auto& o : observers)
                if (auto p = o.lock())
                    listeners.push_back(std::move(p));
        }
        for (auto& x : listeners)
            x->on_connection_error(reason);
    }

    template <bool acquire_lock = true>
    auto write_all(const_buffer data, cancel_token* token = nullptr) -> task<result<void>>
    {
        if constexpr (acquire_lock)
            co_await write_mutex.lock();

        struct write_guard
        {
            async_mutex& mutex;

            ~write_guard()
            {
                if constexpr (acquire_lock)
                    mutex.unlock();
            }
        } guard{write_mutex};

        if (token && token->is_cancelled())
            co_return std::unexpected(make_error(error_code::cancelled, "write cancelled"));
#ifdef CNETMOD_HAS_SSL
        if (ssl)
        {
            auto r = token ? co_await ssl->async_write_all(data, *token)
                           : co_await ssl->async_write_all(data);
            if (!r)
                co_return std::unexpected(transport_error("TLS write", r.error()));
            co_return result<void>{};
        }
#endif
        auto r = token ? co_await cnetmod::async_write_all(ctx, sock, data, *token)
                       : co_await cnetmod::async_write_all(ctx, sock, data);
        if (!r)
            co_return std::unexpected(transport_error("socket write", r.error()));
        co_return result<void>{};
    }

    auto read_some(mutable_buffer data, cancel_token* token = nullptr)
        -> task<result<std::size_t>>
    {
#ifdef CNETMOD_HAS_SSL
        if (ssl)
        {
            auto r = token ? co_await ssl->async_read(data, *token)
                           : co_await ssl->async_read(data);
            if (!r)
                co_return std::unexpected(transport_error("TLS read", r.error()));
            co_return *r;
        }
#endif
        auto r = token ? co_await cnetmod::async_read(ctx, sock, data, *token)
                       : co_await cnetmod::async_read(ctx, sock, data);
        if (!r)
            co_return std::unexpected(transport_error("socket read", r.error()));
        co_return *r;
    }

    auto next_frame(cancel_token* token = nullptr) -> task<result<frame>>
    {
        while (ready.empty())
        {
            std::array<std::byte, 16384> buffer{};
            auto n = co_await read_some(mutable_buffer{buffer.data(), buffer.size()},
                token);
            if (!n)
                co_return std::unexpected(n.error());
            if (*n == 0)
                co_return std::unexpected(make_error(error_code::connection_closed,
                    "peer closed connection", true));
            last_received_ns.store(
                std::chrono::steady_clock::now().time_since_epoch().count());
            auto frames = parser.feed(std::span{buffer.data(), *n});
            if (!frames)
                co_return std::unexpected(frames.error());
            for (auto& f : *frames)
                ready.push_back(std::move(f));
        }
        auto value = std::move(ready.front());
        ready.pop_front();
        co_return value;
    }

    template <bool acquire_lock = true>
    auto send_frame(frame value, cancel_token* token = nullptr) -> task<result<void>>
    {
        auto encoded = encode_frame(value);
        if (!encoded)
            co_return std::unexpected(encoded.error());
        co_return co_await write_all<acquire_lock>(
            const_buffer{encoded->data(), encoded->size()}, token);
    }

    template <bool acquire_lock = true>
    auto send_method(method_frame value, cancel_token* token = nullptr) -> task<result<void>>
    {
        auto f = encode_method(value);
        if (!f)
            co_return std::unexpected(f.error());
        co_return co_await send_frame<acquire_lock>(std::move(*f), token);
    }

    auto read_method(cancel_token* token = nullptr) -> task<result<method_frame>>
    {
        for (;;)
        {
            auto f = co_await next_frame(token);
            if (!f)
                co_return std::unexpected(f.error());
            if (f->type == frame_type::heartbeat)
                continue;
            if (f->type != frame_type::method)
                co_return std::unexpected(make_error(
                    error_code::unexpected_frame, "method expected during handshake"));
            co_return decode_method(*f);
        }
    }
};

protocol_connection::protocol_connection(io_context& ctx)
    : impl_(std::make_unique<impl>(ctx)) {}

protocol_connection::~protocol_connection() = default;

auto protocol_connection::state() const noexcept -> connection_state
{
    return impl_->current.load();
}

auto protocol_connection::negotiated_frame_max() const noexcept
    -> std::uint32_t
{
    return impl_->frame_max;
}

auto protocol_connection::negotiated_channel_max() const noexcept
    -> std::uint16_t
{
    return impl_->channel_max;
}

void protocol_connection::observe(std::weak_ptr<connection_observer> o)
{
    concurrent_containers::exclusive_latch_guard lock{impl_->state_latch};
    impl_->observers.push_back(std::move(o));
}

void protocol_connection::set_return_handler(return_handler h)
{
    impl_->returned = std::move(h);
}

void protocol_connection::set_recovery_strategy(
    std::shared_ptr<recovery_strategy> s)
{
    impl_->recovery = std::move(s);
}

auto protocol_connection::async_connect(connection_options options)
    -> task<result<void>>
{
    cancel_token ignored;
    co_return co_await async_connect(std::move(options), ignored);
}

auto protocol_connection::async_connect(connection_options options,
    cancel_token& token)
    -> task<result<void>>
{
    return async_connect_attempt<false>(std::move(options), token);
}

template <bool Recovery>
auto protocol_connection::async_connect_attempt(connection_options options,
    cancel_token& token) -> task<result<void>>
{
    if (token.is_cancelled())
        co_return std::unexpected(
            make_error(error_code::cancelled, "connect cancelled"));
    if constexpr (!Recovery)
    {
        if (impl_->recovery_active)
            co_return std::unexpected(error{.code = error_code::command_invalid});
    }

    /**
     * @brief Admits a new handshake only after the previous transport is idle.
     *
     * Claim the reader before replacing socket, parser or TLS state. Refuse
     * active writers instead of waiting without a cancellable lock deadline.
     */
    const auto current = state();
    if (current != connection_state::disconnected && current != connection_state::recovering)
        co_return std::unexpected(error{.code = error_code::command_invalid});
    bool reader_available = false;
    if (!impl_->reader_claimed.compare_exchange_strong(reader_available, true, std::memory_order_acq_rel))
        co_return std::unexpected(error{.code = error_code::command_invalid});
    reader_claim_guard handshake_reader{impl_->reader_claimed};
    if (!impl_->write_mutex.try_lock())
        co_return std::unexpected(error{.code = error_code::command_invalid});
    impl_->write_mutex.unlock();

    /**
     * @brief Rolls back transport ownership until the handshake commits.
     *
     * Cleanup cannot suspend or replace the original handshake failure, even
     * when state notification allocates or an observer throws.
     */
    struct handshake_rollback
    {
        impl& connection;
        bool committed = false;

        ~handshake_rollback() noexcept
        {
            if (committed)
                return;
#ifdef CNETMOD_HAS_SSL
            connection.ssl.reset();
            connection.ssl_ctx.reset();
#endif
            connection.sock.close();
            connection.parser.reset();
            connection.ready.clear();
            try
            {
                connection.transition(connection_state::disconnected);
            }
            catch (...)
            {
                // transition publishes the state before invoking observers.
            }
        }
    } rollback{*impl_};

    impl_->options = std::move(options);
    if (impl_->generation == std::numeric_limits<std::uint64_t>::max())
        co_return std::unexpected(error{.code = error_code::connection_closed});
    ++impl_->generation;
    impl_->managed_reader_completion.reset();
    impl_->managed_reader_token.reset();
    /**
     * @brief Discards transport-local state before admitting a new session.
     * A reader cancelled before dispatch may leave decoded frames unconsumed.
     */
    impl_->parser.reset();
    impl_->ready.clear();
    impl_->fail_pending_rpcs();
    impl_->fail_pending_confirmations();
    impl_->delivery_handlers.clear();
    impl_->content.clear();
    impl_->next_channel = 1;
#ifdef CNETMOD_HAS_SSL
    impl_->ssl.reset();
    impl_->ssl_ctx.reset();
#endif
    impl_->sock.close();
    if (impl_->options.automatic_recovery && !impl_->recovery)
        impl_->recovery = std::make_shared<automatic_recovery_strategy>(
            std::make_shared<exponential_backoff>());
    impl_->transition(connection_state::connecting);
    impl_->transport_interrupted = false;
    auto connected = co_await async_connect_happy_eyeballs(
        impl_->ctx, impl_->options.endpoint.host, impl_->options.endpoint.port,
        happy_eyeballs_options{.connect_timeout =
                                   impl_->options.endpoint.connect_timeout},
        token);
    if (!connected)
    {
        impl_->transition(connection_state::disconnected);
        co_return std::unexpected(transport_error("connect", connected.error()));
    }
    impl_->sock = std::move(connected->sock);
#ifdef CNETMOD_HAS_SSL
    if (impl_->options.endpoint.tls.enabled)
    {
        auto context = ssl_context::client();
        if (!context)
            co_return std::unexpected(
                transport_error("TLS context", context.error()));
        impl_->ssl_ctx = std::make_unique<ssl_context>(std::move(*context));
        auto& t = impl_->options.endpoint.tls;
        impl_->ssl_ctx->set_verify_peer(t.verify_peer);
        if (!t.ca_file.empty())
        {
            auto r = impl_->ssl_ctx->load_ca_file(t.ca_file);
            if (!r)
                co_return std::unexpected(transport_error("TLS CA", r.error()));
        }
        else if (t.verify_peer)
            (void)impl_->ssl_ctx->set_default_ca();
        if (!t.certificate_file.empty())
        {
            auto r = impl_->ssl_ctx->load_cert_file(t.certificate_file);
            if (!r)
                co_return std::unexpected(
                    transport_error("TLS certificate", r.error()));
        }
        if (!t.private_key_file.empty())
        {
            auto r = impl_->ssl_ctx->load_key_file(t.private_key_file);
            if (!r)
                co_return std::unexpected(transport_error("TLS key", r.error()));
        }
        impl_->ssl =
            std::make_unique<ssl_stream>(*impl_->ssl_ctx, impl_->ctx, impl_->sock);
        impl_->ssl->set_connect_state();
        impl_->ssl->set_hostname(
            t.server_name.empty() ? impl_->options.endpoint.host : t.server_name);
        auto hs = co_await impl_->ssl->async_handshake(token);
        if (!hs)
            co_return std::unexpected(transport_error("TLS handshake", hs.error()));
    }
#else
    if (impl_->options.endpoint.tls.enabled)
        co_return std::unexpected(
            make_error(error_code::command_invalid,
                "TLS requested but SSL support is disabled"));
#endif
    if (token.is_cancelled())
    {
        impl_->sock.close();
        co_return std::unexpected(
            make_error(error_code::cancelled, "connect cancelled"));
    }
    impl_->transition(connection_state::authenticating);
    auto header = co_await impl_->write_all(
        const_buffer{protocol_header.data(), protocol_header.size()}, &token);
    if (!header)
        co_return header;
    auto start = co_await impl_->read_method(&token);
    if (!start)
        co_return std::unexpected(start.error());
    if (start->class_id != 10 || start->method_id != 10)
        co_return std::unexpected(
            make_error(error_code::unexpected_frame, "expected Connection.Start"));
    reader start_args(start->arguments);
    if (!start_args.u8() || !start_args.u8())
        co_return std::unexpected(
            make_error(error_code::malformed_frame, "truncated Connection.Start"));
    std::size_t table_used = 0;
    auto server_properties = decode_field_table(
        std::span<const std::byte>{start->arguments}.subspan(start_args.pos),
        table_used);
    if (!server_properties)
        co_return std::unexpected(server_properties.error());
    start_args.pos += table_used;
    auto mechanisms = start_args.long_string();
    auto locales = start_args.long_string();
    if (!mechanisms || !locales)
        co_return std::unexpected(
            make_error(error_code::malformed_frame,
                "truncated Connection.Start capabilities"));
    std::string mechanism, response;
    auto auth = impl_->options.credentials.mechanism;
    if (auth == authentication_mechanism::external)
    {
        mechanism = "EXTERNAL";
    }
    else if (auth == authentication_mechanism::plain)
    {
        mechanism = "PLAIN";
        response.push_back('\0');
        response += impl_->options.credentials.username;
        response.push_back('\0');
        response += impl_->options.credentials.password;
    }
    else
        co_return std::unexpected(
            make_error(error_code::access_refused,
                "AMQP 0-9-1 supports PLAIN or EXTERNAL authentication"));
    if (mechanisms->find(mechanism) == std::string::npos)
        co_return std::unexpected(make_error(error_code::access_refused,
            "server does not offer " + mechanism));
    field_table properties;
    properties.values["product"] = std::string("cnetmod");
    properties.values["version"] = std::string("2.0");
    if (!impl_->options.connection_name.empty())
        properties.values["connection_name"] = impl_->options.connection_name;
    auto capabilities = std::make_shared<field_table>();
    capabilities->values["publisher_confirms"] = true;
    capabilities->values["consumer_cancel_notify"] = true;
    capabilities->values["basic.nack"] = true;
    properties.values["capabilities"] = capabilities;
    auto encoded_properties = encode_field_table(properties);
    if (!encoded_properties)
        co_return std::unexpected(encoded_properties.error());
    writer start_ok;
    start_ok.bytes(*encoded_properties);
    start_ok.short_string(mechanism);
    start_ok.long_string(response);
    start_ok.short_string(impl_->options.locale);
    auto sent =
        co_await impl_->send_method({.channel = 0,
                                        .class_id = 10,
                                        .method_id = 11,
                                        .arguments = std::move(start_ok.data)},
            &token);
    if (!sent)
        co_return sent;
    auto tune = co_await impl_->read_method(&token);
    if (!tune)
        co_return std::unexpected(tune.error());
    if (tune->class_id == 10 && tune->method_id == 20)
        co_return std::unexpected(
            make_error(error_code::access_refused,
                "challenge-response Connection.Secure is not supported"));
    if (tune->class_id != 10 || tune->method_id != 30)
        co_return std::unexpected(
            make_error(error_code::unexpected_frame, "expected Connection.Tune"));
    reader tune_args(tune->arguments);
    auto server_channels = tune_args.integer<std::uint16_t>();
    auto server_frame = tune_args.integer<std::uint32_t>();
    auto server_heartbeat = tune_args.integer<std::uint16_t>();
    if (!server_channels || !server_frame || !server_heartbeat)
        co_return std::unexpected(
            make_error(error_code::malformed_frame, "truncated Connection.Tune"));
    auto choose = []<class T>(T requested, T offered)
    {
        if (requested == 0)
            return offered;
        if (offered == 0)
            return requested;
        return std::min(requested, offered);
    };
    impl_->channel_max = choose(impl_->options.channel_max, *server_channels);
    impl_->frame_max =
        std::max(4096u, choose(impl_->options.frame_max, *server_frame));
    impl_->heartbeat = std::chrono::seconds{
        choose(static_cast<std::uint16_t>(impl_->options.heartbeat.count()),
            *server_heartbeat)};
    impl_->parser = frame_parser(impl_->frame_max);
    writer tune_ok;
    tune_ok.integer(impl_->channel_max);
    tune_ok.integer(impl_->frame_max);
    tune_ok.integer(static_cast<std::uint16_t>(impl_->heartbeat.count()));
    if (auto r =
            co_await impl_->send_method({.channel = 0,
                                            .class_id = 10,
                                            .method_id = 31,
                                            .arguments = std::move(tune_ok.data)},
                &token);
        !r)
        co_return r;
    impl_->transition(connection_state::opening);
    writer open;
    open.short_string(impl_->options.virtual_host);
    open.short_string("");
    open.u8(0);
    if (auto r = co_await impl_->send_method({.channel = 0,
                                                 .class_id = 10,
                                                 .method_id = 40,
                                                 .arguments = std::move(open.data)},
            &token);
        !r)
        co_return r;
    auto opened = co_await impl_->read_method(&token);
    if (!opened)
        co_return std::unexpected(opened.error());
    if (opened->class_id != 10 || opened->method_id != 41)
        co_return std::unexpected(make_error(error_code::unexpected_frame,
            "expected Connection.Open-Ok"));
    if (token.is_cancelled())
        co_return std::unexpected(make_error(error_code::cancelled, "connect cancelled"));
    impl_->transition(connection_state::open);
    rollback.committed = true;
    co_return result<void>{};
}

auto protocol_connection::generation() const noexcept -> std::uint64_t
{
    return impl_->generation;
}

auto protocol_connection::async_send(frame value) -> task<result<void>>
{
    const auto attempt = impl_->generation;
    co_await impl_->write_mutex.lock();
    async_lock_guard guard(impl_->write_mutex, std::adopt_lock);
    if (attempt != impl_->generation || state() != connection_state::open)
        co_return std::unexpected(error{.code = error_code::connection_closed});
    co_return co_await impl_->send_frame<false>(std::move(value));
}

auto protocol_connection::async_settle_delivery(std::uint64_t generation, std::uint16_t channel,
    std::uint64_t tag, std::uint16_t method, std::uint8_t flags) -> task<result<void>>
{
    co_await impl_->write_mutex.lock();
    async_lock_guard guard(impl_->write_mutex, std::adopt_lock);
    if (generation != impl_->generation || state() != connection_state::open ||
        !impl_->confirms.contains(channel))
        co_return std::unexpected(error{.code = error_code::channel_closed});
    writer args;
    args.integer(tag);
    args.u8(flags);
    co_return co_await impl_->send_method<false>({.channel = channel, .class_id = 60, .method_id = method, .arguments = std::move(args.data)});
}

auto protocol_connection::async_rpc(method_frame request,
    std::uint16_t expected_class,
    std::uint16_t expected_method)
    -> task<result<method_frame>>
{
    if (state() != connection_state::open)
        co_return std::unexpected(make_error(error_code::connection_closed,
            "connection is not open", true));
    const auto channel = request.channel;
    const auto attempt = impl_->generation;
    bool reader_available = false;
    if (impl_->reader_claimed.compare_exchange_strong(
            reader_available, true, std::memory_order_acq_rel))
    {
        reader_claim_guard reader_claim{impl_->reader_claimed};
        {
            co_await impl_->write_mutex.lock();
            async_lock_guard guard(impl_->write_mutex, std::adopt_lock);
            if (attempt != impl_->generation || state() != connection_state::open)
                co_return std::unexpected(error{.code = error_code::connection_closed});
            if (auto sent = co_await impl_->send_method<false>(std::move(request)); !sent)
                co_return std::unexpected(sent.error());
        }
        for (;;)
        {
            auto reply = co_await impl_->read_method();
            if (!reply)
                co_return std::unexpected(reply.error());
            if (reply->channel == channel && reply->class_id == expected_class &&
                reply->method_id == expected_method)
                co_return *reply;
            if (reply->class_id == 10 && reply->method_id == 50)
                co_return std::unexpected(make_error(error_code::connection_closed,
                    "server closed connection"));
        }
    }
    auto pending = std::make_shared<pending_rpc>();
    pending->expected_class = expected_class;
    pending->expected_method = expected_method;
    {
        concurrent_containers::exclusive_latch_guard lock{impl_->state_latch};
        if (impl_->pending.contains(channel))
            co_return std::unexpected(
                make_error(error_code::command_invalid,
                    "another synchronous method is pending on this channel"));
        impl_->pending[channel] = pending;
    }

    /**
     * @brief Retires this registration on success, error or exception unwinding.
     *
     * Identity matching prevents cleanup from erasing a replacement request.
     * The local shared owner remains alive until after this guard is destroyed.
     */
    struct registration_guard
    {
        impl& connection;
        std::uint16_t channel;
        const std::shared_ptr<pending_rpc>& call;

        ~registration_guard() noexcept
        {
            concurrent_containers::exclusive_latch_guard lock{connection.state_latch};
            const auto found = connection.pending.find(channel);
            if (found != connection.pending.end() && found->second == call)
                connection.pending.erase(found);
        }
    } registration{*impl_, channel, pending};

    result<void> sent;
    {
        co_await impl_->write_mutex.lock();
        async_lock_guard guard(impl_->write_mutex, std::adopt_lock);
        if (attempt != impl_->generation || state() != connection_state::open)
            sent = std::unexpected(error{.code = error_code::connection_closed});
        else
            sent = co_await impl_->send_method<false>(std::move(request));
    }
    if (!sent)
        co_return std::unexpected(sent.error());
    co_return co_await rpc_awaiter{pending};
}

auto protocol_connection::async_send_message(std::uint16_t channel,
    method_frame publish,
    message message, publisher_confirm_tracker* confirmations, std::uint64_t generation)
    -> task<result<std::uint64_t>>
{
    co_await impl_->write_mutex.lock();
    async_lock_guard guard(impl_->write_mutex, std::adopt_lock);
    if (generation != impl_->generation)
        co_return std::unexpected(error{.code = error_code::channel_closed});
    if (state() != connection_state::open)
        co_return std::unexpected(error{.code = error_code::connection_closed});
    auto header = encode_content_header({.channel = channel,
        .class_id = 60,
        .body_size = message.body.size(),
        .properties = message});
    if (!header)
        co_return std::unexpected(header.error());
    auto method = encode_method(publish);
    if (!method)
        co_return std::unexpected(method.error());
    auto method_wire = encode_frame(*method);
    if (!method_wire)
        co_return std::unexpected(method_wire.error());
    auto header_wire = encode_frame(*header);
    if (!header_wire)
        co_return std::unexpected(header_wire.error());
    if (method_wire->size() > impl_->frame_max || header_wire->size() > impl_->frame_max)
        co_return std::unexpected(error{.code = error_code::frame_too_large});
    const auto tag = confirmations ? confirmations->reserve_sequence() : 0;

    /**
     * @brief Invalidates a possibly truncated message before releasing the writer.
     *
     * Shutdown wakes the frame pump without destroying its active TLS stream.
     * The pump remains responsible for joining children and retiring confirmations.
     */
    struct publication_guard
    {
        impl& connection;
        bool complete = false;

        ~publication_guard() noexcept
        {
            if (!complete)
            {
                auto expected = connection_state::open;
                connection.current.compare_exchange_strong(expected, connection_state::closing);
                connection.interrupt_transport();
            }
        }
    } publication{*impl_};

    if (auto sent = co_await impl_->write_all<false>(
            const_buffer{method_wire->data(), method_wire->size()});
        !sent)
        co_return std::unexpected(sent.error());
    if (auto sent = co_await impl_->write_all<false>(
            const_buffer{header_wire->data(), header_wire->size()});
        !sent)
        co_return std::unexpected(sent.error());
    auto max_payload = std::max<std::uint32_t>(1, impl_->frame_max - 8);
    for (std::size_t offset = 0; offset < message.body.size();)
    {
        auto size =
            std::min<std::size_t>(max_payload, message.body.size() - offset);
        frame body{.type = frame_type::body, .channel = channel};
        body.payload.assign(
            message.body.begin() + static_cast<std::ptrdiff_t>(offset),
            message.body.begin() + static_cast<std::ptrdiff_t>(offset + size));
        if (auto r = co_await impl_->send_frame<false>(std::move(body)); !r)
            co_return std::unexpected(r.error());
        offset += size;
    }
    publication.complete = true;
    co_return tag;
}

void protocol_connection::register_delivery_handler(std::uint16_t channel,
    std::string tag,
    delivery_handler handler)
{
    concurrent_containers::exclusive_latch_guard lock{impl_->state_latch};
    impl_->delivery_handlers[channel][std::move(tag)] = std::move(handler);
}

void protocol_connection::unregister_delivery_handler(std::uint16_t channel,
    std::string_view tag)
{
    concurrent_containers::exclusive_latch_guard lock{impl_->state_latch};
    if (auto it = impl_->delivery_handlers.find(channel);
        it != impl_->delivery_handlers.end())
        it->second.erase(std::string(tag));
}

void protocol_connection::retire_channel(std::uint16_t channel, const error& reason) noexcept
{
    std::shared_ptr<publisher_confirm_tracker> tracker;
    {
        concurrent_containers::exclusive_latch_guard lock{impl_->state_latch};
        if (auto it = impl_->confirms.find(channel); it != impl_->confirms.end())
        {
            tracker = std::move(it->second);
            impl_->confirms.erase(it);
        }
        impl_->delivery_handlers.erase(channel);
        impl_->content.erase(channel);
    }
    if (tracker)
        tracker->fail_all(reason);
}

void protocol_connection::abort_subscription(std::uint64_t generation, std::uint16_t channel) noexcept
{
    if (generation != impl_->generation)
        return;
    impl_->current.store(connection_state::closing);
    impl_->interrupt_transport();
    retire_channel(channel, error{.code = error_code::channel_closed});
    if (!impl_->reader_claimed.load(std::memory_order_acquire) && impl_->write_mutex.try_lock())
    {
        impl_->release_transport();
        impl_->write_mutex.unlock();
        impl_->fail_pending_rpcs();
        impl_->fail_pending_confirmations();
        impl_->current.store(connection_state::disconnected);
    }
}

auto protocol_connection::confirm_tracker(std::uint16_t channel)
    -> std::shared_ptr<publisher_confirm_tracker>
{
    concurrent_containers::exclusive_latch_guard lock{impl_->state_latch};
    auto& v = impl_->confirms[channel];
    if (!v)
        v = std::make_shared<publisher_confirm_tracker>();
    return v;
}

auto protocol_connection::topology() -> std::shared_ptr<topology_recorder>
{
    return impl_->topology_state;
}

auto protocol_connection::async_open_channel()
    -> task<result<std::shared_ptr<logical_channel>>>
{
    return async_open_channel_attempt<false>();
}

template <bool Recovery>
auto protocol_connection::async_open_channel_attempt()
    -> task<result<std::shared_ptr<logical_channel>>>
{
    if constexpr (!Recovery)
    {
        if (impl_->recovery_active)
            co_return std::unexpected(error{.code = error_code::command_invalid});
    }
    if (state() != connection_state::open)
        co_return std::unexpected(error{.code = error_code::connection_closed});
    const auto channel = impl_->next_channel;
    if (channel == 0 || (impl_->channel_max && channel > impl_->channel_max))
        co_return std::unexpected(make_error(error_code::invalid_channel,
            "negotiated channel limit reached"));
    /**
     * @brief Advances only admitted identifiers, leaving zero as exhaustion.
     * Rejected requests must not wrap the counter back to an active channel.
     */
    ++impl_->next_channel;
    writer args;
    args.short_string("");
    auto opened = co_await async_rpc({.channel = channel,
                                         .class_id = 20,
                                         .method_id = 10,
                                         .arguments = std::move(args.data)},
        20, 11);
    if (!opened)
        co_return std::unexpected(opened.error());
    co_return std::shared_ptr<logical_channel>(
        new logical_channel(shared_from_this(), channel));
}

auto protocol_connection::async_run(cancel_token& token) -> task<result<void>>
{
    return async_run_with_start(token, []() noexcept {});
}

template <typename OnStarted>
auto protocol_connection::async_run_with_start(cancel_token& token, OnStarted on_started) -> task<result<void>>
{
    for (;;)
    {
        if (token.is_cancelled())
            co_return std::unexpected(
                make_error(error_code::cancelled, "event loop cancelled"));
        bool available = false;
        if (impl_->reader_claimed.compare_exchange_strong(
                available, true, std::memory_order_acq_rel))
            break;
        co_await async_sleep(impl_->ctx, std::chrono::milliseconds{2});
    }
    reader_claim_guard reader_claim{impl_->reader_claimed};
    on_started();

    auto run_children = [&]() -> task<result<void>>
    {
        impl_->last_received_ns.store(
            std::chrono::steady_clock::now().time_since_epoch().count());
        if (impl_->heartbeat.count() <= 0)
            co_return co_await async_receive_frames(token);

        cancel_token receive_token;
        cancel_token heartbeat_token;

        /**
     * @brief Links caller cancellation without sharing a token between I/O tasks.
     */
        struct cancellation_link
        {
            cancel_token& parent;
            cancel_token& child;

            cancellation_link(cancel_token& parent, cancel_token& child) noexcept
                : parent(parent), child(child)
            {
                if (!parent.register_callback(this, [](void* value) noexcept
                        {
                            static_cast<cancellation_link*>(value)->child.cancel();
                        }))
                    child.cancel();
            }

            ~cancellation_link()
            {
                (void)parent.complete_callback(this);
                parent.finish_callback(this);
            }
        } link{token, receive_token};

        auto receive = [&]() -> task<result<void>>
        {
            try
            {
                auto result = co_await async_receive_frames(receive_token);
                impl_->interrupt_transport();
                heartbeat_token.cancel();
                co_return result;
            }
            catch (...)
            {
                impl_->interrupt_transport();
                heartbeat_token.cancel();
                throw;
            }
        };
        auto heartbeat = [&]() -> task<result<void>>
        {
            try
            {
                while (!heartbeat_token.is_cancelled() && state() == connection_state::open)
                {
                    auto waited = co_await async_timer_wait(impl_->ctx,
                        std::max(std::chrono::seconds{1}, impl_->heartbeat / 2), heartbeat_token);
                    if (heartbeat_token.is_cancelled())
                        co_return result<void>{};
                    if (!waited)
                    {
                        receive_token.cancel();
                        co_return std::unexpected(transport_error("heartbeat timer", waited.error()));
                    }
                    const auto elapsed = std::chrono::steady_clock::now().time_since_epoch().count() -
                        impl_->last_received_ns.load();
                    if (elapsed > std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                                      impl_->heartbeat * 2)
                                      .count())
                    {
                        receive_token.cancel();
                        co_return std::unexpected(make_error(error_code::timeout, "heartbeat timeout"));
                    }
                    auto sent = co_await impl_->send_frame(
                        {.type = frame_type::heartbeat, .channel = 0}, &heartbeat_token);
                    if (!sent && !heartbeat_token.is_cancelled())
                    {
                        receive_token.cancel();
                        co_return sent;
                    }
                }
                co_return result<void>{};
            }
            catch (...)
            {
                receive_token.cancel();
                throw;
            }
        };
        auto [received, sent] = co_await when_all(receive(), heartbeat());
        if (!sent)
            co_return sent;
        co_return received;
    };

    result<void> outcome;
    std::exception_ptr failure;
    try
    {
        outcome = co_await run_children();
    }
    catch (...)
    {
        failure = std::current_exception();
    }
    /**
     * @brief Wakes active writes and joins the writer before releasing TLS state.
     *
     * Reader children have already settled. Retire pending operations outside
     * the write lock because completion callbacks may submit new operations.
     */
    impl_->interrupt_transport();
    co_await impl_->write_mutex.lock();
    impl_->release_transport();
    impl_->current.store(connection_state::disconnected);
    impl_->write_mutex.unlock();
    impl_->fail_pending_rpcs();
    impl_->fail_pending_confirmations();
    if (failure)
        std::rethrow_exception(failure);
    co_return outcome;
}

auto protocol_connection::async_run_session(cancel_token& token, std::function<void()> on_ready) -> task<result<void>>
{
    if (token.is_cancelled())
        co_return std::unexpected(error{.code = error_code::cancelled});
    if (state() != connection_state::open || impl_->recovery_active || impl_->reader_claimed.load(std::memory_order_acquire))
        co_return std::unexpected(error{.code = error_code::command_invalid});
    auto saved = impl_->topology_state->snapshot();
    auto pump_token = std::make_shared<cancel_token>();
    impl_->managed_reader_token = pump_token;

    struct cancellation_link
    {
        cancel_token& parent;
        cancel_token& child;

        cancellation_link(cancel_token& parent, cancel_token& child) noexcept : parent(parent), child(child)
        {
            if (!parent.register_callback(this, [](void* raw) noexcept
                    {
                        static_cast<cancellation_link*>(raw)->child.cancel();
                    }))
                child.cancel();
        }

        ~cancellation_link()
        {
            (void)parent.complete_callback(this);
            parent.finish_callback(this);
        }
    } cancellation{token, *pump_token};

    struct replay_admission
    {
        impl& connection;
        bool active = true;

        void finish() noexcept
        {
            if (!std::exchange(active, false))
                return;
            connection.recovery_cancellation = nullptr;
            connection.recovery_active = false;
            connection.recovery_completed.done();
        }

        ~replay_admission()
        {
            finish();
        }
    } admission{*impl_};

    impl_->recovery_completed.add();
    impl_->recovery_active = true;
    impl_->recovery_cancellation = &token;
    impl::reader_completion startup;
    startup.started.add();
    auto reader = [&]() -> task<result<void>>
    {
        try
        {
            auto outcome = co_await async_run_with_start(*pump_token, [&]() noexcept
                {
                    startup.notify_started();
                });
            startup.outcome = outcome;
            startup.notify_started();
            co_return outcome;
        }
        catch (...)
        {
            startup.failure = std::current_exception();
            startup.notify_started();
            throw;
        }
    };
    auto replay = [&]() -> task<result<void>>
    {
        try
        {
            co_await startup.started.wait();
            if (startup.failure)
                std::rethrow_exception(startup.failure);
            result<void> outcome;
            if (pump_token->is_cancelled())
                outcome = std::unexpected(error{.code = error_code::cancelled});
            else if (!startup.outcome)
                outcome = startup.outcome;
            else if (!saved.exchanges.empty() || !saved.queues.empty() || !saved.bindings.empty() || !saved.consumers.empty())
                outcome = co_await async_replay_topology(saved, token);
            admission.finish();
            if (!outcome)
                pump_token->cancel();
            else if (on_ready)
                on_ready();
            co_return outcome;
        }
        catch (...)
        {
            admission.finish();
            pump_token->cancel();
            throw;
        }
    };
    auto [received, restored] = co_await when_all(reader(), replay());
    if (!restored)
        co_return restored;
    co_return received;
}

auto protocol_connection::async_receive_frames(cancel_token& token) -> task<result<void>>
{
    while (!token.is_cancelled() && state() == connection_state::open)
    {
        auto received = co_await impl_->next_frame(&token);
        if (!received)
        {
            impl_->transition(connection_state::disconnected);
            impl_->notify(received.error());
            co_return std::unexpected(received.error());
        }
        auto& f = *received;
        if (f.type == frame_type::heartbeat)
            continue;
        if (f.type == frame_type::method)
        {
            auto method = decode_method(f);
            if (!method)
                co_return std::unexpected(method.error());
            std::shared_ptr<pending_rpc> rpc;
            {
                concurrent_containers::exclusive_latch_guard lock{impl_->state_latch};
                auto it = impl_->pending.find(f.channel);
                if (it != impl_->pending.end() &&
                    it->second->expected_class == method->class_id &&
                    it->second->expected_method == method->method_id)
                {
                    rpc = it->second;
                    impl_->pending.erase(it);
                }
            }
            if (rpc)
            {
                rpc->outcome = *method;
                if (rpc->waiter)
                    rpc->waiter.resume();
                continue;
            }
            if (method->class_id == 60 &&
                (method->method_id == 80 || method->method_id == 120))
            {
                reader in(method->arguments);
                auto tag = in.integer<std::uint64_t>();
                auto bits = in.u8();
                if (tag && bits)
                {
                    auto found = impl_->confirms.find(f.channel);
                    if (found != impl_->confirms.end())
                    {
                        auto tracker = found->second;
                        tracker->settle(*tag, method->method_id == 80, (*bits & 1) != 0);
                    }
                }
                continue;
            }
            if (method->class_id == 60 &&
                (method->method_id == 60 || method->method_id == 50))
            {
                inbound_content content;
                content.type = method->method_id == 60
                    ? inbound_content::kind::delivery
                    : inbound_content::kind::returned;
                if (content.type == inbound_content::kind::delivery)
                {
                    reader d(method->arguments);
                    auto consumer = d.short_string();
                    auto tag = d.integer<std::uint64_t>();
                    auto flags = d.u8();
                    auto ex = d.short_string();
                    auto route = d.short_string();
                    if (!consumer || !tag || !flags || !ex || !route)
                        co_return std::unexpected(make_error(error_code::malformed_frame,
                            "truncated Basic.Deliver"));
                    content.delivered.consumer_tag = *consumer;
                    content.delivered.delivery_tag = *tag;
                    content.delivered.redelivered = (*flags & 1) != 0;
                    content.delivered.exchange = *ex;
                    content.delivered.routing_key = *route;
                }
                else
                {
                    reader r(method->arguments);
                    auto code = r.integer<std::uint16_t>();
                    auto text = r.short_string();
                    auto ex = r.short_string();
                    auto route = r.short_string();
                    if (!code || !text || !ex || !route)
                        co_return std::unexpected(make_error(error_code::malformed_frame,
                            "truncated Basic.Return"));
                    content.returned.reply_code = *code;
                    content.returned.reply_text = *text;
                    content.returned.exchange = *ex;
                    content.returned.routing_key = *route;
                }
                impl_->content[f.channel] = std::move(content);
                continue;
            }
            if (method->class_id == 60 && method->method_id == 30)
            {
                reader in(method->arguments);
                auto tag = in.short_string();
                auto bits = in.u8();
                if (!tag || !bits)
                    co_return std::unexpected(make_error(
                        error_code::malformed_frame, "truncated server Basic.Cancel"));
                unregister_delivery_handler(f.channel, *tag);
                impl_->topology_state->forget_consumer(*tag);
                if ((*bits & 1) == 0)
                {
                    writer ok;
                    ok.short_string(*tag);
                    (void)co_await impl_->send_method({.channel = f.channel,
                        .class_id = 60,
                        .method_id = 31,
                        .arguments = std::move(ok.data)});
                }
                continue;
            }
            if (method->class_id == 20 && method->method_id == 40)
            {
                reader in(method->arguments);
                auto code = in.integer<std::uint16_t>();
                auto text = in.short_string();
                auto cls = in.integer<std::uint16_t>();
                auto id = in.integer<std::uint16_t>();
                auto reason = reply_error(code.value_or(0),
                    text.value_or("server closed channel"),
                    cls.value_or(0), id.value_or(0));
                retire_channel(f.channel, reason);
                (void)co_await impl_->send_method(
                    {.channel = f.channel, .class_id = 20, .method_id = 41});
                std::shared_ptr<pending_rpc> failed;
                {
                    concurrent_containers::exclusive_latch_guard lock{impl_->state_latch};
                    if (auto it = impl_->pending.find(f.channel);
                        it != impl_->pending.end())
                    {
                        failed = it->second;
                        impl_->pending.erase(it);
                    }
                }
                if (failed)
                {
                    failed->outcome = std::unexpected(reason);
                    if (failed->waiter)
                        failed->waiter.resume();
                }
                continue;
            }
            if (method->class_id == 10 && method->method_id == 50)
            {
                reader in(method->arguments);
                auto code = in.integer<std::uint16_t>();
                auto text = in.short_string();
                auto cls = in.integer<std::uint16_t>();
                auto id = in.integer<std::uint16_t>();
                writer ok;
                (void)co_await impl_->send_method({.channel = 0,
                    .class_id = 10,
                    .method_id = 51,
                    .arguments = std::move(ok.data)});
                auto reason = reply_error(code.value_or(0),
                    text.value_or("server closed connection"),
                    cls.value_or(0), id.value_or(0));
                std::vector<std::shared_ptr<pending_rpc>> pending_calls;
                std::vector<std::shared_ptr<publisher_confirm_tracker>> trackers;
                {
                    concurrent_containers::exclusive_latch_guard lock{impl_->state_latch};
                    for (auto& [channel, pending] : impl_->pending)
                        pending_calls.push_back(pending);
                    impl_->pending.clear();
                    for (auto& [channel, tracker] : impl_->confirms)
                        trackers.push_back(tracker);
                    impl_->confirms.clear();
                }
                for (auto& pending : pending_calls)
                {
                    pending->outcome = std::unexpected(reason);
                    if (pending->waiter)
                        pending->waiter.resume();
                }
                for (auto& tracker : trackers)
                    tracker->fail_all(reason);
                impl_->transition(connection_state::disconnected);
                impl_->notify(reason);
                co_return std::unexpected(reason);
            }
            continue;
        }
        auto content_it = impl_->content.find(f.channel);
        if (content_it == impl_->content.end())
            continue;
        if (f.type == frame_type::header)
        {
            auto header = decode_content_header(f);
            if (!header)
                co_return std::unexpected(header.error());
            content_it->second.expected = header->body_size;
            if (content_it->second.type == inbound_content::kind::delivery)
                content_it->second.delivered.message = std::move(header->properties);
            else
                content_it->second.returned.message = std::move(header->properties);
            if (content_it->second.expected == 0)
            {
                auto content = std::move(content_it->second);
                impl_->content.erase(content_it);
                if (content.type == inbound_content::kind::delivery)
                {
                    delivery_handler handler;
                    {
                        concurrent_containers::shared_latch_guard lock{impl_->state_latch};
                        if (auto c = impl_->delivery_handlers.find(f.channel);
                            c != impl_->delivery_handlers.end())
                            if (auto h = c->second.find(content.delivered.consumer_tag);
                                h != c->second.end())
                                handler = h->second;
                    }
                    if (handler)
                        handler(content.delivered);
                }
                else if (impl_->returned)
                    impl_->returned(content.returned);
            }
        }
        else if (f.type == frame_type::body)
        {
            auto& content = content_it->second;
            auto& body = content.type == inbound_content::kind::delivery
                ? content.delivered.message.body
                : content.returned.message.body;
            body.insert(body.end(), f.payload.begin(), f.payload.end());
            if (body.size() >= content.expected)
            {
                auto completed_node = impl_->content.extract(content_it);
                auto& completed = completed_node.mapped();
                if (completed.type == inbound_content::kind::delivery)
                {
                    delivery_handler handler;
                    {
                        concurrent_containers::shared_latch_guard lock{impl_->state_latch};
                        if (auto c = impl_->delivery_handlers.find(f.channel);
                            c != impl_->delivery_handlers.end())
                            if (auto h = c->second.find(completed.delivered.consumer_tag);
                                h != c->second.end())
                                handler = h->second;
                    }
                    if (handler)
                        handler(completed.delivered);
                }
                else if (impl_->returned)
                    impl_->returned(completed.returned);
            }
        }
    }
    if (token.is_cancelled())
        co_return std::unexpected(
            make_error(error_code::cancelled, "event loop cancelled"));
    if (impl_->transport_interrupted)
        co_return std::unexpected(error{.code = error_code::connection_closed});
    co_return result<void>{};
}

auto protocol_connection::async_close(std::string text) -> task<result<void>>
{
    if (impl_->recovery_cancellation)
        impl_->recovery_cancellation->cancel();
    std::shared_ptr<cancel_token> managed_reader;
    {
        concurrent_containers::shared_latch_guard lock{impl_->state_latch};
        managed_reader = impl_->managed_reader_token;
    }
    if (managed_reader)
        managed_reader->cancel();
    co_await impl_->recovery_completed.wait();
    if (auto completion = impl_->managed_reader_completion)
    {
        co_await completion->completed.wait();
        if (completion->failure)
            std::rethrow_exception(completion->failure);
        if (!completion->outcome)
            co_return completion->outcome;
    }
    if (state() == connection_state::disconnected)
        co_return result<void>{};
    if (impl_->transport_interrupted)
    {
        const auto attempt = impl_->generation;
        if (impl_->reader_claimed.load(std::memory_order_acquire))
            co_return result<void>{};
        co_await impl_->write_mutex.lock();
        if (attempt != impl_->generation || state() == connection_state::disconnected ||
            impl_->reader_claimed.load(std::memory_order_acquire))
        {
            impl_->write_mutex.unlock();
            co_return result<void>{};
        }
        impl_->release_transport();
        impl_->write_mutex.unlock();
        impl_->fail_pending_rpcs();
        impl_->fail_pending_confirmations();
        impl_->current.store(connection_state::disconnected);
        co_return result<void>{};
    }
    impl_->transition(connection_state::closing);
    writer args;
    args.integer<std::uint16_t>(200);
    args.short_string(text);
    args.integer<std::uint16_t>(0);
    args.integer<std::uint16_t>(0);
    auto r = co_await impl_->send_method({.channel = 0,
        .class_id = 10,
        .method_id = 50,
        .arguments = std::move(args.data)});
    impl_->sock.close();
    impl_->transition(connection_state::disconnected);
    if (impl_->transport_interrupted)
        co_return result<void>{};
    co_return r;
}

auto protocol_connection::async_recover(cancel_token& token)
    -> task<result<void>>
{
    if (token.is_cancelled())
        co_return std::unexpected(error{.code = error_code::cancelled});
    if (impl_->recovery_active || state() != connection_state::disconnected || impl_->reader_claimed.load(std::memory_order_acquire))
        co_return std::unexpected(error{.code = error_code::command_invalid});
    if (!impl_->recovery)
        co_return std::unexpected(make_error(error_code::connection_closed,
            "automatic recovery is disabled"));

    /**
     * @brief Owns the recovery episode across backoff and handshake suspension.
     */
    struct recovery_guard
    {
        impl& connection;

        ~recovery_guard() noexcept
        {
            connection.recovery_cancellation = nullptr;
            connection.recovery_active = false;
            connection.recovery_completed.done();
        }
    } recovery{*impl_};

    impl_->recovery_completed.add();
    impl_->recovery_cancellation = &token;
    impl_->recovery_active = true;
    auto saved = impl_->topology_state->snapshot();
    reconnect_context context{};
    error last =
        make_error(error_code::connection_closed, "recovery did not start", true);
    while (!token.is_cancelled())
    {
        auto delay = impl_->recovery->next_delay(context);
        if (!delay)
            co_return std::unexpected(last);
        impl_->transition(connection_state::recovering);
        auto waited = co_await async_timer_wait(impl_->ctx, *delay, token);
        if (token.is_cancelled())
        {
            auto expected = connection_state::recovering;
            impl_->current.compare_exchange_strong(expected, connection_state::disconnected);
            break;
        }
        if (!waited)
        {
            auto expected = connection_state::recovering;
            impl_->current.compare_exchange_strong(expected, connection_state::disconnected);
            co_return std::unexpected(transport_error("recovery timer", waited.error()));
        }
        auto connected = co_await async_connect_attempt<true>(impl_->options, token);
        if (!connected)
        {
            last = connected.error();
            context.previous_delay = *delay;
            ++context.attempt;
            continue;
        }

        /**
         * @brief Owns the connected transport until reader dispatch succeeds.
         * No suspension occurs during dispatch, so rollback owns idle I/O.
         */
        struct dispatch_rollback
        {
            impl& connection;
            bool committed = false;

            ~dispatch_rollback() noexcept
            {
                if (committed)
                    return;
#ifdef CNETMOD_HAS_SSL
                connection.ssl.reset();
                connection.ssl_ctx.reset();
#endif
                connection.sock.close();
                connection.parser.reset();
                connection.ready.clear();
                connection.current.store(connection_state::disconnected);
            }
        } dispatch{*impl_};

        auto managed_reader = std::make_shared<cancel_token>();
        auto completion = std::make_shared<impl::reader_completion>();

        /**
         * @brief Releases connection ownership before publishing completion.
         * Dispatch failure and execution share this completion ticket.
         */
        struct reader_ticket
        {
            std::shared_ptr<protocol_connection> owner;
            std::shared_ptr<impl::reader_completion> completion;
            bool registered = false;

            reader_ticket(std::shared_ptr<protocol_connection> owner,
                std::shared_ptr<impl::reader_completion> completion)
                : owner(std::move(owner)), completion(std::move(completion)) {}

            ~reader_ticket()
            {
                owner.reset();
                if (registered)
                {
                    completion->notify_started();
                    completion->completed.done();
                }
            }
        };

        auto ticket = std::make_shared<reader_ticket>(shared_from_this(), completion);
        {
            concurrent_containers::exclusive_latch_guard lock{impl_->state_latch};
            impl_->managed_reader_token = managed_reader;
            impl_->managed_reader_completion = completion;
        }
        completion->started.add();
        completion->completed.add();
        ticket->registered = true;
        spawn_guarded(impl_->ctx, [](std::shared_ptr<reader_ticket> ticket, std::shared_ptr<cancel_token> managed_reader) -> task<void>
            {
                auto outcome = co_await ticket->owner->async_run_with_start(*managed_reader,
                    [completion = ticket->completion]() noexcept
                    {
                        completion->notify_started();
                    });
                if (!managed_reader->is_cancelled())
                    ticket->completion->outcome = std::move(outcome);
                ticket->completion->notify_started();
            }(ticket, std::move(managed_reader)),
            [ticket](std::exception_ptr failure)
            {
                ticket->completion->failure = failure;
                ticket->owner->impl_->interrupt_transport();
                ticket->completion->notify_started();
            });
        if (completion->failure)
            std::rethrow_exception(completion->failure);
        dispatch.committed = true;
        if (!impl_->recovery->restore_topology())
            co_return result<void>{};

        /**
         * @brief Forwards recovery cancellation while topology RPCs are active.
         */
        struct topology_cancellation
        {
            cancel_token& parent;
            std::shared_ptr<cancel_token> child;

            topology_cancellation(cancel_token& parent, std::shared_ptr<cancel_token> child) noexcept
                : parent(parent), child(std::move(child))
            {
                if (!parent.register_callback(this, [](void* raw) noexcept
                        {
                            static_cast<topology_cancellation*>(raw)->child->cancel();
                        }))
                    this->child->cancel();
            }

            ~topology_cancellation()
            {
                (void)parent.complete_callback(this);
                parent.finish_callback(this);
            }
        } cancellation{token, impl_->managed_reader_token};

        // The first topology RPC must run only after the reader owns the transport.
        co_await completion->started.wait();
        if (completion->failure)
            std::rethrow_exception(completion->failure);
        co_return co_await async_replay_topology(saved, token);
    }
    co_return std::unexpected(
        make_error(error_code::cancelled, "recovery cancelled"));
}

/**
 * @brief Replays a snapshot without owning transport startup or retry policy.
 * The caller retains the snapshot and a running reader until replay completes.
 */
auto protocol_connection::async_replay_topology(const topology_snapshot& saved, cancel_token& token)
    -> task<result<void>>
{
    auto recovery_error = [&token](error failure) -> error
    {
        if (token.is_cancelled())
            return error{.code = error_code::cancelled};
        return failure;
    };
    if (token.is_cancelled())
        co_return std::unexpected(error{.code = error_code::cancelled});

    /**
         * @brief Preserves the last complete topology until replay commits.
         * Rollback only moves shared ownership and cannot allocate or suspend.
         */
    struct topology_transaction
    {
        std::shared_ptr<topology_recorder>& destination;
        std::shared_ptr<topology_recorder> original;
        bool committed = false;

        explicit topology_transaction(std::shared_ptr<topology_recorder>& destination)
            : destination(destination), original(destination)
        {
            destination = std::make_shared<topology_recorder>();
        }

        ~topology_transaction() noexcept
        {
            if (!committed)
                destination = std::move(original);
        }
    } topology{impl_->topology_state};

    auto channel = co_await async_open_channel_attempt<true>();
    if (!channel)
        co_return std::unexpected(recovery_error(channel.error()));
    std::map<std::string, std::string, std::less<>> queue_names;
    for (auto& item : saved.exchanges)
        if (auto r = co_await (*channel)->async_declare_exchange(item.options,
                item.arguments);
            !r)
            co_return std::unexpected(recovery_error(r.error()));
    for (auto& item : saved.queues)
    {
        auto declared = co_await (*channel)->async_declare_queue(item.options,
            item.arguments);
        if (!declared)
            co_return std::unexpected(recovery_error(declared.error()));
        queue_names[item.server_name] = declared->name;
    }
    for (auto& item : saved.bindings)
    {
        auto options = item.options;
        if (auto found = queue_names.find(options.queue);
            found != queue_names.end())
            options.queue = found->second;
        if (auto r = co_await (*channel)->async_bind_queue(std::move(options),
                item.arguments);
            !r)
            co_return std::unexpected(recovery_error(r.error()));
    }
    for (auto& item : saved.consumers)
    {
        auto options = item.options;
        if (auto found = queue_names.find(options.queue);
            found != queue_names.end())
            options.queue = found->second;
        auto subscription = item.acknowledged_handler
            ? (*channel)->async_consume_acknowledged(std::move(options), item.acknowledged_handler, item.arguments)
            : (*channel)->async_consume(std::move(options), item.handler, item.arguments);
        if (auto r = co_await std::move(subscription);
            !r)
            co_return std::unexpected(recovery_error(r.error()));
    }
    if (token.is_cancelled())
        co_return std::unexpected(error{.code = error_code::cancelled});
    topology.committed = true;
    co_return result<void>{};
}
} // namespace cnetmod::amqp091
