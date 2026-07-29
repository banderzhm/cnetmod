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
import cnetmod.coro.spawn;
import cnetmod.coro.timer;
import cnetmod.executor.async_op;
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
    std::mutex state_mutex;
    std::atomic<std::int64_t> last_received_ns{0};
    std::shared_ptr<std::atomic_bool> run_active;
    std::atomic_bool reader_claimed{false};
    std::shared_ptr<cancel_token> managed_reader_token;
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

    void transition(connection_state value)
    {
        current.store(value);
        std::vector<std::shared_ptr<connection_observer>> listeners;
        {
            std::scoped_lock l(state_mutex);
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
            std::scoped_lock l(state_mutex);
            for (auto& o : observers)
                if (auto p = o.lock())
                    listeners.push_back(std::move(p));
        }
        for (auto& x : listeners)
            x->on_connection_error(reason);
    }

    auto write_all(const_buffer data) -> task<result<void>>
    {
        co_await write_mutex.lock();
        async_lock_guard guard(write_mutex, std::adopt_lock);
#ifdef CNETMOD_HAS_SSL
        if (ssl)
        {
            auto r = co_await ssl->async_write_all(data);
            if (!r)
                co_return std::unexpected(transport_error("TLS write", r.error()));
            co_return result<void>{};
        }
#endif
        auto r = co_await cnetmod::async_write_all(ctx, sock, data);
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
            auto r = co_await ssl->async_read(data);
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

    auto send_frame(frame value) -> task<result<void>>
    {
        auto encoded = encode_frame(value);
        if (!encoded)
            co_return std::unexpected(encoded.error());
        co_return co_await write_all(
            const_buffer{encoded->data(), encoded->size()});
    }

    auto send_method(method_frame value) -> task<result<void>>
    {
        auto f = encode_method(value);
        if (!f)
            co_return std::unexpected(f.error());
        co_return co_await send_frame(std::move(*f));
    }

    auto read_method() -> task<result<method_frame>>
    {
        for (;;)
        {
            auto f = co_await next_frame();
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
    std::scoped_lock l(impl_->state_mutex);
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
    if (token.is_cancelled())
        co_return std::unexpected(
            make_error(error_code::cancelled, "connect cancelled"));
    impl_->options = std::move(options);
    if (impl_->options.automatic_recovery && !impl_->recovery)
        impl_->recovery = std::make_shared<automatic_recovery_strategy>(
            std::make_shared<exponential_backoff>());
    impl_->transition(connection_state::connecting);
    auto connected = co_await async_connect_happy_eyeballs(
        impl_->ctx, impl_->options.endpoint.host, impl_->options.endpoint.port,
        happy_eyeballs_options{.connect_timeout =
                                   impl_->options.endpoint.connect_timeout});
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
        auto hs = co_await impl_->ssl->async_handshake();
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
        const_buffer{protocol_header.data(), protocol_header.size()});
    if (!header)
        co_return header;
    auto start = co_await impl_->read_method();
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
            .arguments = std::move(start_ok.data)});
    if (!sent)
        co_return sent;
    auto tune = co_await impl_->read_method();
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
                .arguments = std::move(tune_ok.data)});
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
            .arguments = std::move(open.data)});
        !r)
        co_return r;
    auto opened = co_await impl_->read_method();
    if (!opened)
        co_return std::unexpected(opened.error());
    if (opened->class_id != 10 || opened->method_id != 41)
        co_return std::unexpected(make_error(error_code::unexpected_frame,
            "expected Connection.Open-Ok"));
    impl_->transition(connection_state::open);
    co_return result<void>{};
}

auto protocol_connection::async_send(frame value) -> task<result<void>>
{
    co_return co_await impl_->send_frame(std::move(value));
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
    bool reader_available = false;
    if (impl_->reader_claimed.compare_exchange_strong(
            reader_available, true, std::memory_order_acq_rel))
    {
        reader_claim_guard reader_claim{impl_->reader_claimed};
        if (auto sent = co_await impl_->send_method(std::move(request)); !sent)
            co_return std::unexpected(sent.error());
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
        std::scoped_lock l(impl_->state_mutex);
        if (impl_->pending.contains(channel))
            co_return std::unexpected(
                make_error(error_code::command_invalid,
                    "another synchronous method is pending on this channel"));
        impl_->pending[channel] = pending;
    }
    auto sent = co_await impl_->send_method(std::move(request));
    if (!sent)
    {
        {
            std::scoped_lock l(impl_->state_mutex);
            impl_->pending.erase(channel);
        }
        co_return std::unexpected(sent.error());
    }
    co_return co_await rpc_awaiter{pending};
}

auto protocol_connection::async_send_message(std::uint16_t channel,
    method_frame publish,
    message message)
    -> task<result<void>>
{
    if (auto r = co_await impl_->send_method(std::move(publish)); !r)
        co_return r;
    auto header = encode_content_header({.channel = channel,
        .class_id = 60,
        .body_size = message.body.size(),
        .properties = message});
    if (!header)
        co_return std::unexpected(header.error());
    if (auto r = co_await impl_->send_frame(std::move(*header)); !r)
        co_return r;
    auto max_payload = std::max<std::uint32_t>(1, impl_->frame_max - 8);
    for (std::size_t offset = 0; offset < message.body.size();)
    {
        auto size =
            std::min<std::size_t>(max_payload, message.body.size() - offset);
        frame body{.type = frame_type::body, .channel = channel};
        body.payload.assign(
            message.body.begin() + static_cast<std::ptrdiff_t>(offset),
            message.body.begin() + static_cast<std::ptrdiff_t>(offset + size));
        if (auto r = co_await impl_->send_frame(std::move(body)); !r)
            co_return r;
        offset += size;
    }
    co_return result<void>{};
}

void protocol_connection::register_delivery_handler(std::uint16_t channel,
    std::string tag,
    delivery_handler handler)
{
    std::scoped_lock l(impl_->state_mutex);
    impl_->delivery_handlers[channel][std::move(tag)] = std::move(handler);
}

void protocol_connection::unregister_delivery_handler(std::uint16_t channel,
    std::string_view tag)
{
    std::scoped_lock l(impl_->state_mutex);
    if (auto it = impl_->delivery_handlers.find(channel);
        it != impl_->delivery_handlers.end())
        it->second.erase(std::string(tag));
}

auto protocol_connection::confirm_tracker(std::uint16_t channel)
    -> std::shared_ptr<publisher_confirm_tracker>
{
    std::scoped_lock l(impl_->state_mutex);
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
    auto channel = impl_->next_channel++;
    if (channel == 0 || (impl_->channel_max && channel > impl_->channel_max))
        co_return std::unexpected(make_error(error_code::invalid_channel,
            "negotiated channel limit reached"));
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
    impl_->run_active = std::make_shared<std::atomic_bool>(true);
    impl_->last_received_ns.store(
        std::chrono::steady_clock::now().time_since_epoch().count());
    auto run_active = impl_->run_active;
    auto weak = weak_from_this();
    auto interval = impl_->heartbeat;
    if (interval.count() > 0)
        spawn(impl_->ctx, [weak, run_active, interval]() -> task<void>
            {
                while (run_active->load())
                {
                    auto self = weak.lock();
                    if (!self || self->state() != connection_state::open)
                        co_return;
                    co_await async_sleep(self->impl_->ctx,
                        std::max(std::chrono::seconds{1}, interval / 2));
                    if (!run_active->load())
                        co_return;
                    self = weak.lock();
                    if (!self || self->state() != connection_state::open)
                        co_return;
                    auto now = std::chrono::steady_clock::now().time_since_epoch().count();
                    auto last = self->impl_->last_received_ns.load();
                    if (now - last >
                        std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                            interval * 2)
                            .count())
                    {
                        self->impl_->sock.close();
                        co_return;
                    }
                    auto sent = co_await self->impl_->send_frame(
                        {.type = frame_type::heartbeat, .channel = 0});
                    if (!sent)
                        co_return;
                }
            }());
    while (!token.is_cancelled() && state() == connection_state::open)
    {
        auto received = co_await impl_->next_frame(&token);
        if (!received)
        {
            run_active->store(false);
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
                std::scoped_lock l(impl_->state_mutex);
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
                    confirm_tracker(f.channel)->settle(*tag, method->method_id == 80,
                        (*bits & 1) != 0);
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
                (void)co_await impl_->send_method(
                    {.channel = f.channel, .class_id = 20, .method_id = 41});
                std::shared_ptr<pending_rpc> failed;
                {
                    std::scoped_lock l(impl_->state_mutex);
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
                confirm_tracker(f.channel)->fail_all(reason);
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
                run_active->store(false);
                std::vector<std::shared_ptr<pending_rpc>> pending_calls;
                std::vector<std::shared_ptr<publisher_confirm_tracker>> trackers;
                {
                    std::scoped_lock l(impl_->state_mutex);
                    for (auto& [channel, pending] : impl_->pending)
                        pending_calls.push_back(pending);
                    impl_->pending.clear();
                    for (auto& [channel, tracker] : impl_->confirms)
                        trackers.push_back(tracker);
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
                        std::scoped_lock l(impl_->state_mutex);
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
                if (content.type == inbound_content::kind::delivery)
                {
                    delivery_handler handler;
                    {
                        std::scoped_lock l(impl_->state_mutex);
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
                impl_->content.erase(content_it);
            }
        }
    }
    run_active->store(false);
    if (token.is_cancelled())
        co_return std::unexpected(
            make_error(error_code::cancelled, "event loop cancelled"));
    co_return result<void>{};
}

auto protocol_connection::async_close(std::string text) -> task<result<void>>
{
    if (state() == connection_state::disconnected)
        co_return result<void>{};
    std::shared_ptr<cancel_token> managed_reader;
    {
        std::scoped_lock lock(impl_->state_mutex);
        managed_reader = impl_->managed_reader_token;
    }
    if (managed_reader)
        managed_reader->cancel();
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
    co_return r;
}

auto protocol_connection::async_recover(cancel_token& token)
    -> task<result<void>>
{
    if (!impl_->recovery)
        co_return std::unexpected(make_error(error_code::connection_closed,
            "automatic recovery is disabled"));
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
        co_await async_sleep(impl_->ctx, *delay);
        if (token.is_cancelled())
            break;
        auto connected = co_await async_connect(impl_->options, token);
        if (!connected)
        {
            last = connected.error();
            context.previous_delay = *delay;
            ++context.attempt;
            continue;
        }
        auto managed_reader = std::make_shared<cancel_token>();
        {
            std::scoped_lock lock(impl_->state_mutex);
            impl_->managed_reader_token = managed_reader;
        }
        auto self = shared_from_this();
        spawn(impl_->ctx, [self = std::move(self), managed_reader]() -> task<void>
            {
                (void)co_await self->async_run(*managed_reader);
            }());
        if (!impl_->recovery->restore_topology())
            co_return result<void>{};
        impl_->topology_state->clear();
        auto channel = co_await async_open_channel();
        if (!channel)
            co_return std::unexpected(channel.error());
        std::map<std::string, std::string, std::less<>> queue_names;
        for (auto& item : saved.exchanges)
            if (auto r = co_await (*channel)->async_declare_exchange(item.options,
                    item.arguments);
                !r)
                co_return std::unexpected(r.error());
        for (auto& item : saved.queues)
        {
            auto declared = co_await (*channel)->async_declare_queue(item.options,
                item.arguments);
            if (!declared)
                co_return std::unexpected(declared.error());
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
                co_return std::unexpected(r.error());
        }
        for (auto& item : saved.consumers)
        {
            auto options = item.options;
            if (auto found = queue_names.find(options.queue);
                found != queue_names.end())
                options.queue = found->second;
            if (auto r = co_await (*channel)->async_consume(
                    std::move(options), item.handler, item.arguments);
                !r)
                co_return std::unexpected(r.error());
        }
        co_return result<void>{};
    }
    co_return std::unexpected(
        make_error(error_code::cancelled, "recovery cancelled"));
}
} // namespace cnetmod::amqp091
