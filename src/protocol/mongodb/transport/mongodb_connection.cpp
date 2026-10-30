module;
#include <cnetmod/config.hpp>
#include <cnetmod/version.hpp>
module cnetmod.protocol.mongodb;

import std;
import cnetmod.core.buffer;
import cnetmod.core.dns;
import cnetmod.executor.async_op;
import cnetmod.coro.cancel;
import cnetmod.coro.timer;
import cnetmod.io.io_context;
#ifdef CNETMOD_HAS_SSL
import cnetmod.core.ssl;
#endif
import :error;
import :bson_document;
import :connection_options;
import :authentication;
import :oidc_credentials;
import :wire_protocol;
import :scram_sha256;
import :connection;

namespace cnetmod::mongodb {
namespace {
    /**
     * Queues cancellation without resuming a protocol coroutine under the token
     * registration lock. Awaiting this bridge retires the registration and joins
     * any queued notification before the connection can be reused or destroyed.
     */
    class connection_cancellation final
    {
    public:
        connection_cancellation(io_context& io, connection& owner, cancel_token& token,
            cancel_token* transport = nullptr) noexcept
            : io_(io), owner_(owner), token_(token), transport_(transport)
        {
            node_.callback_arg = this;
            node_.callback = [](void* value)
            {
                auto& self = *static_cast<connection_cancellation*>(value);
                if (self.transport_)
                    self.transport_->cancel();
                self.owner_.cancel_active_command();
                self.dispatched_ = true;
                const auto waiter = self.waiter_;
                if (waiter)
                    waiter.resume();
            };
            auto notify = [](void* value) noexcept
            {
                auto& self = *static_cast<connection_cancellation*>(value);
                self.io_.post_node_raw(&self.node_);
            };
            if (!token_.register_callback(this, notify))
                notify(this);
        }

        connection_cancellation(const connection_cancellation&) = delete;
        auto operator=(const connection_cancellation&) -> connection_cancellation& = delete;

        auto await_ready() noexcept -> bool
        {
            return token_.complete_callback(this) || dispatched_;
        }

        void await_suspend(std::coroutine_handle<> waiter) noexcept
        {
            waiter_ = waiter;
        }

        void await_resume() noexcept
        {
            token_.finish_callback(this);
        }

    private:
        io_context& io_;
        connection& owner_;
        cancel_token& token_;
        cancel_token* transport_;
        post_node node_;
        std::coroutine_handle<> waiter_;
        bool dispatched_ = false;
    };

    auto integer(const bson_value* value) -> std::optional<std::int64_t>
    {
        if (!value)
            return {};
        if (auto v = value->get_if<std::int32_t>())
            return *v;
        if (auto v = value->get_if<std::int64_t>())
            return *v;
        if (auto v = value->get_if<double>(); v && std::isfinite(*v))
            return static_cast<std::int64_t>(*v);
        return {};
    }

    auto boolean(const bson_value* value) -> std::optional<bool>
    {
        if (!value)
            return {};
        if (auto v = value->get_if<bool>())
            return *v;
        if (auto v = integer(value))
            return *v != 0;
        return {};
    }

    auto text(const bson_value* value) -> std::optional<std::string_view>
    {
        if (!value)
            return {};
        if (auto v = value->get_if<std::string>())
            return *v;
        return {};
    }

    auto command_error(const bson_document& response) -> std::optional<error>
    {
        auto ok_value = response.find("ok");
        bool ok = false;
        if (auto v = ok_value ? ok_value->get_if<double>() : nullptr)
            ok = *v == 1.0;
        else if (auto v = integer(ok_value))
            ok = *v == 1;
        if (ok)
            return {};
        error result = make_error(error_code::command_failed,
            std::string(text(response.find("errmsg")).value_or("MongoDB command failed")));
        if (auto code = integer(response.find("code")))
            result.server_code = static_cast<std::int32_t>(*code);
        if (auto name = text(response.find("codeName")))
            result.server_code_name = *name;
        if (auto labels = response.find("errorLabels"); labels && labels->as_array())
            for (const auto& label : *labels->as_array())
                if (auto name = label.get_if<std::string>())
                    result.labels.emplace(*name, *name);
        return result;
    }

} // namespace

connection::connection(io_context& context) noexcept : context_(context) {}

connection::~connection()
{
    close();
}

auto connection::connect(connection_options options) -> task<result<void>>
{
    return connect_impl<false>(std::move(options), nullptr);
}

auto connection::connect(connection_options options, cancel_token& cancellation) -> task<result<void>>
{
    if (cancellation.is_cancelled())
        co_return std::unexpected(make_error(error_code::operation_cancelled,
            "MongoDB connection was cancelled"));
    if (connecting_ || command_in_progress_)
        co_return std::unexpected(make_error(error_code::protocol_error,
            "MongoDB connection already has an active operation"));
    cancel_token transport;
    connection_cancellation notification{context_, *this, cancellation, &transport};
    result<void> outcome;
    std::exception_ptr failure;
    try
    {
        outcome = co_await connect_impl<true>(std::move(options), &transport);
    }
    catch (...)
    {
        failure = std::current_exception();
    }
    connecting_ = true;
    command_in_progress_ = true;
    co_await notification;
    command_in_progress_ = false;
    connecting_ = false;
    if (failure)
        std::rethrow_exception(failure);
    if (cancellation.is_cancelled())
    {
        close();
        co_return std::unexpected(make_error(error_code::operation_cancelled,
            "MongoDB connection was cancelled"));
    }
    co_return outcome;
}

template <bool Cancellable>
auto connection::connect_impl(connection_options options, cancel_token* transport) -> task<result<void>>
{
    if (connecting_ || command_in_progress_)
        co_return std::unexpected(make_error(error_code::protocol_error,
            "MongoDB connection already has an active operation"));
    connecting_ = true;
    bool completed = false;

    /**
     * A connection attempt owns its transport through exception unwinding.
     * Reject reentry before touching resources and retire partial setup unless
     * authentication completed successfully.
     */
    struct connection_attempt_guard
    {
        connection& owner;
        bool& active;
        bool& completed;

        ~connection_attempt_guard()
        {
            if (!completed)
                owner.close();
            active = false;
        }
    } guard{*this, connecting_, completed};

    close();
    if (options.host.empty() || options.max_message_bytes < 1024 ||
        options.max_bson_document_bytes < 5 || options.max_bson_document_bytes > options.max_message_bytes)
        co_return std::unexpected(make_error(error_code::connection_failed,
            "invalid MongoDB connection options"));
    if (options.authentication == authentication_mechanism::aws || options.authentication == authentication_mechanism::oidc ||
        options.authentication == authentication_mechanism::x509)
    {
        constexpr std::string_view default_source{"admin"};
        if (!options.tls || (options.authentication_database != external_authentication_database && (options.authentication_database_explicit || options.authentication_database != default_source)))
            co_return std::unexpected(make_error(error_code::authentication_failed, "cloud authentication requires TLS and $external"));
        options.authentication_database = external_authentication_database;
    }
    if ((options.authentication == authentication_mechanism::oidc || options.authentication == authentication_mechanism::x509) && !options.password.empty())
        co_return std::unexpected(make_error(error_code::authentication_failed, "OIDC/X509 must not specify a password"));
    if (options.authentication == authentication_mechanism::oidc && options.oidc_provider &&
        (options.oidc_application_environment != oidc_environment::none || !options.oidc_token_resource.empty()))
        co_return std::unexpected(make_error(error_code::authentication_failed, "OIDC callback conflicts with workload environment options"));
    if (options.authentication == authentication_mechanism::aws && (!options.username.empty() || !options.password.empty()))
        co_return std::unexpected(make_error(error_code::authentication_failed, "AWS credentials must come from a provider or environment"));
    options_ = std::move(options);
    happy_eyeballs_options connect_options;
    connect_options.connect_timeout = options_.connect_timeout;
    connect_options.socket_opts.no_delay = true;
    auto connected = co_await (Cancellable
            ? async_connect_happy_eyeballs(context_, options_.host, options_.port, connect_options, *transport)
            : async_connect_happy_eyeballs(context_, options_.host, options_.port, connect_options));
    if (!connected)
        co_return std::unexpected(make_error(error_code::connection_failed,
            "MongoDB TCP connect failed: " + connected.error().message()));
    socket_ = std::move(connected->sock);
#ifdef CNETMOD_HAS_SSL
    if (options_.tls)
    {
        auto context = ssl_context::client();
        if (!context)
        {
            close();
            co_return std::unexpected(make_error(error_code::tls_failed,
                "MongoDB TLS context failed: " + context.error().message()));
        }
        tls_context_ = std::make_unique<ssl_context>(std::move(*context));
        tls_context_->set_verify_peer(options_.tls_verify);
        if (!options_.tls_ca_file.empty())
        {
            auto loaded = tls_context_->load_ca_file(options_.tls_ca_file);
            if (!loaded)
            {
                close();
                co_return std::unexpected(make_error(error_code::tls_failed,
                    "MongoDB TLS CA loading failed: " + loaded.error().message()));
            }
        }
        else if (options_.tls_verify)
        {
            auto loaded = tls_context_->set_default_ca();
            if (!loaded)
            {
                close();
                co_return std::unexpected(make_error(error_code::tls_failed,
                    "MongoDB system CA loading failed: " + loaded.error().message()));
            }
        }
        if (!options_.tls_cert_file.empty())
        {
            auto loaded = tls_context_->load_cert_file(options_.tls_cert_file);
            if (!loaded)
            {
                close();
                co_return std::unexpected(make_error(error_code::tls_failed,
                    "MongoDB TLS certificate loading failed: " + loaded.error().message()));
            }
        }
        if (!options_.tls_key_file.empty())
        {
            auto loaded = tls_context_->load_key_file(options_.tls_key_file);
            if (!loaded)
            {
                close();
                co_return std::unexpected(make_error(error_code::tls_failed,
                    "MongoDB TLS private key loading failed: " + loaded.error().message()));
            }
        }
        tls_stream_ = std::make_unique<ssl_stream>(*tls_context_, context_, socket_);
        tls_stream_->set_connect_state();
        tls_stream_->set_hostname(options_.tls_sni.empty() ? options_.host : options_.tls_sni);
        auto handshake = co_await (Cancellable ? tls_stream_->async_handshake(*transport)
                                               : tls_stream_->async_handshake());
        if (!handshake)
        {
            close();
            co_return std::unexpected(make_error(error_code::tls_failed,
                "MongoDB TLS handshake failed: " + handshake.error().message()));
        }
    }
#else
    if (options_.tls)
    {
        close();
        co_return std::unexpected(make_error(error_code::tls_failed,
            "MongoDB TLS requested but OpenSSL support is unavailable"));
    }
#endif
    if constexpr (Cancellable)
        if (transport->is_cancelled())
            co_return std::unexpected(make_error(error_code::operation_cancelled,
                "MongoDB connection was cancelled"));
    connected_ = true;
    std::string operating_system;
#if defined(_WIN32)
    operating_system = "Windows";
#elif defined(__APPLE__)
    operating_system = "Darwin";
#elif defined(__linux__)
    operating_system = "Linux";
#else
    operating_system = "unknown";
#endif
    bson_document client_metadata{
        {"driver", bson_document{{"name", "cnetmod"}, {"version", CNETMOD_VERSION_STRING}}},
        {"os", bson_document{{"type", std::move(operating_system)}}},
        {"application", bson_document{{"name", "cnetmod"}}}};
    bson_document hello{{"hello", std::int32_t{1}}, {"helloOk", true},
        {"client", std::move(client_metadata)}};
    bson_array offered_compressors;
    constexpr std::array<std::uint8_t, 3> default_order{compressor_zstd, compressor_snappy, compressor_zlib};
    const auto preferred = options_.compression_preference.empty() ? std::span<const std::uint8_t>{default_order} : std::span<const std::uint8_t>{options_.compression_preference};
    for (const auto codec : preferred)
    {
        switch (codec)
        {
        case compressor_zstd:
#ifdef CNETMOD_HAS_ZSTD
            if (options_.enable_zstd_compression)
                offered_compressors.emplace_back(compressor_zstd_name);
#endif
            break;
        case compressor_snappy:
#ifdef CNETMOD_HAS_SNAPPY
            if (options_.enable_snappy_compression)
                offered_compressors.emplace_back(compressor_snappy_name);
#endif
            break;
        case compressor_zlib:
#ifdef CNETMOD_HAS_ZLIB
            if (options_.enable_zlib_compression)
                offered_compressors.emplace_back(compressor_zlib_name);
#endif
            break;
        default:
            co_return std::unexpected(make_error(error_code::protocol_error, "invalid MongoDB compression preference"));
        }
    }
    if (!offered_compressors.empty())
        hello.append("compression", offered_compressors);
    if (options_.load_balanced)
        hello.append("loadBalanced", true);
    if (options_.authentication == authentication_mechanism::oidc && options_.oidc_provider)
    {
        auto token = co_await options_.oidc_provider->peek();
        if (token)
        {
            oidc_access_token_ = *token;
            auto payload = encode_bson_document(bson_document{{"jwt", *token}});
            if (!payload)
                co_return std::unexpected(payload.error());
            hello.append("speculativeAuthenticate", bson_document{{"saslStart", std::int32_t{1}}, {"mechanism", oidc_mechanism_name}, {"db", external_authentication_database}, {"payload", bson_binary{0, std::move(*payload)}}});
        }
    }
    auto hello_response = co_await execute_command("admin", std::move(hello));
    if (!hello_response)
    {
        auto failure = hello_response.error();
        close();
        co_return std::unexpected(std::move(failure));
    }
    hello_response_ = *hello_response;
    if (options_.load_balanced && !hello_response->contains("serviceId"))
        co_return std::unexpected(make_error(error_code::protocol_error, "loadBalanced server has no serviceId"));
    if (auto v = integer(hello_response->find("minWireVersion")))
        capabilities_.minimum_wire_version = static_cast<std::int32_t>(*v);
    if (auto v = integer(hello_response->find("maxWireVersion")))
        capabilities_.maximum_wire_version = static_cast<std::int32_t>(*v);
    if (auto v = integer(hello_response->find("maxBsonObjectSize")))
        capabilities_.maximum_bson_object_size = static_cast<std::int32_t>(*v);
    if (auto v = integer(hello_response->find("maxMessageSizeBytes")))
        capabilities_.maximum_message_size_bytes = static_cast<std::int32_t>(*v);
    if (auto v = integer(hello_response->find("maxWriteBatchSize")))
        capabilities_.maximum_write_batch_size = static_cast<std::int32_t>(*v);
    capabilities_.writable_primary = boolean(hello_response->find("isWritablePrimary")).value_or(false);
    capabilities_.sessions_supported = hello_response->contains("logicalSessionTimeoutMinutes");
    // Select only a codec compiled into this client and explicitly offered;
    // server order cannot silently override the client's zstd preference.
    if (auto compressors = hello_response->find("compression"); compressors && compressors->as_array())
        for (const auto& offer : offered_compressors)
        {
            const auto& name = *offer.get_if<std::string>();
            if (std::ranges::any_of(*compressors->as_array(), [&](const bson_value& candidate)
                    {
                        const auto text = candidate.get_if<std::string>();
                        return text && *text == name;
                    }))
            {
                selected_compressor_ = name == compressor_zstd_name ? compressor_zstd : (name == compressor_snappy_name ? compressor_snappy : compressor_zlib);
                break;
            }
        }
    capabilities_.selected_compressor = selected_compressor_;
    if (hello_response->contains("setName"))
        capabilities_.server_type = capabilities_.writable_primary ? "replica_set_primary" : "replica_set_member";
    else
        capabilities_.server_type = "standalone";
    if (capabilities_.maximum_wire_version < 7)
    {
        close();
        co_return std::unexpected(make_error(
            error_code::protocol_error, "MongoDB server does not support required wire protocol version"));
    }
    options_.max_message_bytes = std::min(options_.max_message_bytes,
        static_cast<std::size_t>(std::max(capabilities_.maximum_message_size_bytes, 1024)));
    options_.max_bson_document_bytes = std::min(options_.max_bson_document_bytes,
        static_cast<std::size_t>(std::max(capabilities_.maximum_bson_object_size, 5)));
    if constexpr (Cancellable)
        if (transport->is_cancelled())
            co_return std::unexpected(make_error(error_code::operation_cancelled,
                "MongoDB connection was cancelled"));
    auto authentication = co_await authenticate();
    if (!authentication)
    {
        auto failure = authentication.error();
        close();
        co_return std::unexpected(std::move(failure));
    }
    authenticated_ = true;
    completed = true;
    co_return result<void>{};
}

auto connection::command(std::string_view database, bson_document document)
    -> task<result<bson_document>>
{
    if (!connected_ || !authenticated_)
        co_return std::unexpected(make_error(error_code::connection_closed,
            "MongoDB connection is not ready"));
    co_return co_await execute_command(database, std::move(document));
}

auto connection::command(std::string_view database, bson_document document,
    std::vector<document_sequence> sequences) -> task<result<bson_document>>
{
    if (!connected_ || !authenticated_)
        co_return std::unexpected(make_error(error_code::connection_closed,
            "MongoDB connection is not ready"));
    co_return co_await execute_command(database, std::move(document), std::move(sequences));
}

auto connection::command(bson_document document) -> task<result<bson_document>>
{
    co_return co_await command(options_.database, std::move(document));
}

auto connection::command_stream(std::string_view database, bson_document document,
    command_stream_handler on_message) -> task<result<void>>
{
    if (!connected_ || !authenticated_)
        co_return std::unexpected(make_error(error_code::connection_closed,
            "MongoDB connection is not ready"));
    if (!on_message)
        co_return std::unexpected(make_error(error_code::protocol_error,
            "MongoDB command stream requires a message handler"));
    if (command_in_progress_)
        co_return std::unexpected(make_error(error_code::protocol_error,
            "concurrent commands on one MongoDB connection are not allowed"));

    command_in_progress_ = true;
    command_io_cancel_.reset();
    command_cancel_requested_.store(false, std::memory_order_release);
    active_command_.store(true, std::memory_order_release);
    result<void> streamed;
    try
    {
        streamed = co_await execute_command_stream(database, std::move(document),
            std::move(on_message));
    }
    catch (...)
    {
        /**
         * A failed consumer may leave unread exhaust responses on the wire.
         * Retire the transport and command state before propagating its error.
         */
        close();
        throw;
    }
    active_command_.store(false, std::memory_order_release);
    command_in_progress_ = false;
    if (command_cancel_requested_.load(std::memory_order_acquire))
    {
        close();
        co_return std::unexpected(make_error(error_code::operation_cancelled,
            "MongoDB command stream was cancelled"));
    }
    co_return streamed;
}

auto connection::execute_command_with_timer(std::string database,
    bson_document document, cancel_token& timer_token,
    std::vector<document_sequence> sequences)
    -> task<result<bson_document>>
{
    try
    {
        auto response = co_await execute_command_without_deadline(
            database, std::move(document), std::move(sequences));
        timer_token.cancel();
        co_return response;
    }
    catch (...)
    {
        timer_token.cancel();
        throw;
    }
}

auto connection::command_timeout_watchdog(cancel_token& timer_token,
    std::atomic<bool>& timed_out) -> task<int>
{
    std::expected<void, std::error_code> waited;
    try
    {
        waited = co_await async_timer_wait(context_, options_.command_timeout, timer_token);
    }
    catch (...)
    {
        command_io_cancel_.cancel();
        throw;
    }
    if (!waited && !timer_token.is_cancelled())
    {
        command_io_cancel_.cancel();
        throw std::system_error(waited.error());
    }
    if (waited)
    {
        timed_out.store(true, std::memory_order_release);
        /**
         * Cancel the pending operation explicitly. Closing a descriptor alone
         * does not complete a suspended epoll read; transport cleanup follows
         * after the command and watchdog have both settled.
         */
        command_io_cancel_.cancel_due_to_deadline();
    }
    co_return 0;
}

auto connection::execute_command(std::string_view database, bson_document document,
    std::vector<document_sequence> sequences)
    -> task<result<bson_document>>
{
    if (!connected_ || !socket_.is_open())
        co_return std::unexpected(make_error(error_code::connection_closed,
            "MongoDB socket is closed"));
    if (command_in_progress_)
        co_return std::unexpected(make_error(error_code::protocol_error,
            "concurrent commands on one MongoDB connection are not allowed"));
    command_in_progress_ = true;

    struct reset_guard
    {
        bool* flag;
        std::atomic<bool>* active;
        std::optional<std::chrono::steady_clock::time_point>* deadline;

        ~reset_guard()
        {
            *flag = false;
            active->store(false, std::memory_order_release);
            deadline->reset();
        }
    } command_guard{&command_in_progress_, &active_command_, &command_deadline_};

    if (options_.command_timeout.count() > 0)
        command_deadline_ = std::chrono::steady_clock::now() + options_.command_timeout;

    if (options_.command_timeout <= std::chrono::milliseconds::zero())
    {
        command_io_cancel_.reset();
        command_cancel_requested_.store(false, std::memory_order_release);
        active_command_.store(true, std::memory_order_release);
        auto response = co_await execute_command_without_deadline(
            database, std::move(document), std::move(sequences));
        active_command_.store(false, std::memory_order_release);
        if (command_cancel_requested_.load(std::memory_order_acquire))
        {
            close();
            co_return std::unexpected(make_error(error_code::operation_cancelled,
                "MongoDB command was cancelled"));
        }
        co_return response;
    }

    cancel_token timer_token;
    std::atomic<bool> timed_out{false};
    command_io_cancel_.reset();
    command_cancel_requested_.store(false, std::memory_order_release);
    active_command_.store(true, std::memory_order_release);

    auto operation = execute_command_with_timer(
        std::string(database), std::move(document), timer_token, std::move(sequences));
    auto watchdog = command_timeout_watchdog(timer_token, timed_out);
    auto [response, watchdog_result] = co_await when_all(
        std::move(operation), std::move(watchdog));
    (void)watchdog_result;
    active_command_.store(false, std::memory_order_release);
    if (timed_out.load(std::memory_order_acquire))
    {
        close();
        co_return std::unexpected(make_error(error_code::operation_timed_out,
            "MongoDB command exceeded configured timeout"));
    }
    if (command_cancel_requested_.load(std::memory_order_acquire))
    {
        close();
        co_return std::unexpected(make_error(error_code::operation_cancelled,
            "MongoDB command was cancelled"));
    }
    co_return response;
}

auto connection::execute_command_without_deadline(std::string_view database,
    bson_document document, std::vector<document_sequence> sequences) -> task<result<bson_document>>
{
    if (!connected_ || !socket_.is_open())
        co_return std::unexpected(make_error(error_code::connection_closed,
            "MongoDB socket is closed"));
    std::optional<bson_document> retry_document;
    std::vector<document_sequence> retry_sequences;
    if (options_.authentication == authentication_mechanism::oidc && !reauthenticating_)
    {
        retry_document = document;
        retry_sequences = sequences;
    }
    document.set("$db", std::string(database));
    if (next_request_id_ <= 0 || next_request_id_ == std::numeric_limits<std::int32_t>::max())
        next_request_id_ = 1;
    auto request_id = next_request_id_++;
    for (const auto& sequence : sequences)
        if (sequence.documents.size() > static_cast<std::size_t>(capabilities_.maximum_write_batch_size))
            co_return std::unexpected(make_error(error_code::message_too_large,
                "MongoDB document sequence exceeds negotiated write batch size"));
    auto encoded = encode_command_message(request_id, document, sequences, options_.max_message_bytes, 0,
        bson_limits{.max_document_bytes = options_.max_bson_document_bytes});
    if (!encoded)
        co_return std::unexpected(encoded.error());
    std::vector<std::byte> compressed;
    std::span<const std::byte> outgoing = *encoded;
    if (selected_compressor_ && encoded->size() >= options_.compression_minimum_bytes)
    {
        auto compressed_result = encode_compressed_message(*encoded, *selected_compressor_,
            options_.max_message_bytes);
        if (!compressed_result)
            co_return std::unexpected(compressed_result.error());
        compressed = std::move(*compressed_result);
        outgoing = compressed;
    }
    auto written = co_await write_all(outgoing);
    if (!written)
    {
        close();
        co_return std::unexpected(written.error());
    }
    auto response = co_await receive_response(request_id);
    if (!response)
    {
        auto failure = response.error();
        close();
        co_return std::unexpected(std::move(failure));
    }
    if (auto failure = command_error(*response))
    {
        if (failure->server_code == 391 && retry_document && !reauthenticating_)
        {
            // Keep the original command guard and watchdog alive across SASL.
            // A second 391 is returned rather than triggering an infinite loop.
            reauthenticating_ = true;

            struct reauth_guard
            {
                bool& active;

                ~reauth_guard()
                {
                    active = false;
                }
            } guard{reauthenticating_};

            if (options_.oidc_provider)
                co_await options_.oidc_provider->invalidate(oidc_access_token_);
            auto authenticated = co_await authenticate_oidc();
            if (!authenticated)
            {
                close();
                co_return std::unexpected(authenticated.error());
            }
            co_return co_await execute_command_without_deadline(database, std::move(*retry_document), std::move(retry_sequences));
        }
        co_return std::unexpected(std::move(*failure));
    }
    co_return response;
}

auto connection::execute_command_stream(std::string_view database,
    bson_document document, command_stream_handler on_message)
    -> task<result<void>>
{
    document.set("$db", std::string(database));
    if (next_request_id_ <= 0 ||
        next_request_id_ == std::numeric_limits<std::int32_t>::max())
        next_request_id_ = 1;
    const auto request_id = next_request_id_++;
    auto encoded = encode_command_message(request_id, document, options_.max_message_bytes,
        op_message_exhaust_allowed);
    if (!encoded)
        co_return std::unexpected(encoded.error());
    std::vector<std::byte> compressed;
    std::span<const std::byte> outgoing = *encoded;
    if (selected_compressor_ && encoded->size() >= options_.compression_minimum_bytes)
    {
        auto compressed_result = encode_compressed_message(*encoded, *selected_compressor_,
            options_.max_message_bytes);
        if (!compressed_result)
            co_return std::unexpected(compressed_result.error());
        compressed = std::move(*compressed_result);
        outgoing = compressed;
    }
    auto written = co_await write_all(outgoing);
    if (!written)
    {
        close();
        co_return std::unexpected(written.error());
    }

    auto expected_response_to = request_id;
    for (;;)
    {
        auto message = co_await receive_message(expected_response_to);
        if (!message)
        {
            close();
            co_return std::unexpected(message.error());
        }
        if (auto failure = command_error(message->body))
        {
            close();
            co_return std::unexpected(std::move(*failure));
        }
        const bool more_to_come = (message->flags & op_message_more_to_come) != 0;
        auto consumed = co_await on_message(std::move(message->body));
        if (!consumed)
        {
            close();
            co_return std::unexpected(consumed.error());
        }
        if (!more_to_come)
            co_return result<void>{};
        /**
         * In an exhaust sequence, each server message becomes the correlation
         * parent of the following message. MongoDB therefore sets responseTo
         * to the previous server requestId, not to the original client id.
         */
        expected_response_to = message->header.request_id;
    }
}

void connection::cancel_active_command() noexcept
{
    if (!active_command_.load(std::memory_order_acquire))
        return;
    command_cancel_requested_.store(true, std::memory_order_release);
    command_io_cancel_.cancel();
}

auto connection::receive_message(std::int32_t expected) -> task<result<decoded_message>>
{
    std::array<std::byte, 16> header_bytes{};
    auto header_read = co_await read_exact(header_bytes);
    if (!header_read)
        co_return std::unexpected(header_read.error());
    auto header = decode_message_header(header_bytes);
    if (!header)
        co_return std::unexpected(header.error());
    if (header->message_length < 21 || static_cast<std::size_t>(header->message_length) > options_.max_message_bytes)
        co_return std::unexpected(make_error(error_code::message_too_large,
            "MongoDB response length exceeds configured maximum"));
    std::vector<std::byte> message(static_cast<std::size_t>(header->message_length));
    std::copy(header_bytes.begin(), header_bytes.end(), message.begin());
    auto body_read = co_await read_exact(std::span(message).subspan(16));
    if (!body_read)
        co_return std::unexpected(body_read.error());
    if (header->operation_code == op_compressed)
    {
        auto expanded = decode_compressed_message(message, options_.max_message_bytes);
        if (!expanded)
            co_return std::unexpected(expanded.error());
        message = std::move(*expanded);
    }
    auto decoded = decode_command_message(message, options_.max_message_bytes,
        bson_limits{.max_document_bytes = options_.max_bson_document_bytes});
    if (!decoded)
        co_return std::unexpected(decoded.error());
    if (decoded->header.response_to != expected)
        co_return std::unexpected(make_error(error_code::protocol_error,
            "MongoDB response correlation id mismatch"));
    co_return std::move(*decoded);
}

auto connection::receive_response(std::int32_t expected) -> task<result<bson_document>>
{
    auto decoded = co_await receive_message(expected);
    if (!decoded)
        co_return std::unexpected(decoded.error());
    if ((decoded->flags & op_message_more_to_come) != 0)
        co_return std::unexpected(make_error(error_code::protocol_error,
            "streaming MongoDB responses are not supported by command()"));
    co_return std::move(decoded->body);
}

auto connection::read_exact(std::span<std::byte> destination) -> task<result<void>>
{
    std::size_t position{};
    while (position < destination.size())
    {
#ifdef CNETMOD_HAS_SSL
        std::expected<std::size_t, std::error_code> read;
        if (tls_stream_)
            read = co_await tls_stream_->async_read(
                mutable_buffer{destination.data() + position,
                    destination.size() - position},
                command_io_cancel_);
        else
            read = co_await async_read(context_, socket_,
                mutable_buffer{destination.data() + position,
                    destination.size() - position},
                command_io_cancel_);
#else
        auto read = co_await async_read(context_, socket_,
            mutable_buffer{destination.data() + position, destination.size() - position}, command_io_cancel_);
#endif
        if (!read || *read == 0)
            co_return std::unexpected(make_error(error_code::connection_closed,
                read ? "MongoDB peer closed the connection" : "MongoDB read failed: " + read.error().message()));
        position += *read;
    }
    co_return result<void>{};
}

auto connection::write_all(std::span<const std::byte> source) -> task<result<void>>
{
#ifdef CNETMOD_HAS_SSL
    std::expected<void, std::error_code> written;
    if (tls_stream_)
        written = co_await tls_stream_->async_write_all(
            const_buffer{source.data(), source.size()}, command_io_cancel_);
    else
        written = co_await async_write_all(context_, socket_,
            const_buffer{source.data(), source.size()}, command_io_cancel_);
#else
    auto written = co_await async_write_all(context_, socket_, const_buffer{source.data(), source.size()}, command_io_cancel_);
#endif
    if (!written)
        co_return std::unexpected(make_error(error_code::connection_closed,
            "MongoDB write failed: " + written.error().message()));
    co_return result<void>{};
}

auto connection::command(std::string_view database, bson_document document, cancel_token& cancellation) -> task<result<bson_document>>
{
    if (cancellation.is_cancelled())
        co_return std::unexpected(make_error(error_code::operation_cancelled,
            "MongoDB command was cancelled"));
    if (connecting_ || command_in_progress_)
        co_return std::unexpected(make_error(error_code::protocol_error,
            "MongoDB connection already has an active operation"));
    connection_cancellation notification{context_, *this, cancellation};
    result<bson_document> outcome;
    std::exception_ptr failure;
    try
    {
        outcome = co_await command(database, std::move(document));
    }
    catch (...)
    {
        failure = std::current_exception();
    }
    command_in_progress_ = true;
    co_await notification;
    command_in_progress_ = false;
    if (failure)
    {
        /**
         * The exception may follow a partial exchange. Retire the transport
         * only after its cancellation notification has relinquished ownership.
         */
        close();
        std::rethrow_exception(failure);
    }
    if (cancellation.is_cancelled())
        co_return std::unexpected(make_error(error_code::operation_cancelled,
            "MongoDB command was cancelled"));
    co_return outcome;
}

auto connection::ping(cancel_token& cancellation) -> task<result<void>>
{
    try
    {
        auto response = co_await command("admin", bson_document{{"ping", std::int32_t{1}}}, cancellation);
        if (!response)
            co_return std::unexpected(response.error());
        co_return result<void>{};
    }
    catch (...)
    {
        // Constructing the cancellable command task can itself allocate before
        // its notification bridge exists. Retire this ping's transport as well.
        close();
        throw;
    }
}

auto connection::ping() -> task<result<void>>
{
    auto response = co_await command("admin", bson_document{{"ping", std::int32_t{1}}});
    if (!response)
        co_return std::unexpected(response.error());
    co_return result<void>{};
}

void connection::close() noexcept
{
#ifdef CNETMOD_HAS_SSL
    tls_stream_.reset();
    tls_context_.reset();
#endif
    socket_.close();
    connected_ = false;
    authenticated_ = false;
    command_in_progress_ = false;
    capabilities_ = {};
    hello_response_ = {};
    oidc_access_token_.clear();
    selected_compressor_.reset();
    active_command_.store(false, std::memory_order_release);
    std::fill(options_.password.begin(), options_.password.end(), '\0');
    options_.password.clear();
}

auto connection::is_open() const noexcept -> bool
{
    return connected_ && socket_.is_open();
}

auto connection::secure_channel() const noexcept -> bool
{
#ifdef CNETMOD_HAS_SSL
    return tls_stream_ != nullptr;
#else
    return false;
#endif
}

auto connection::capabilities() const noexcept -> const server_capabilities&
{
    return capabilities_;
}

auto connection::hello_response() const noexcept -> const bson_document&
{
    return hello_response_;
}

} // namespace cnetmod::mongodb
