module;
#include <cnetmod/config.hpp>
module cnetmod.protocol.mongodb;

import std;
import cnetmod.core.buffer;
import cnetmod.core.dns;
import cnetmod.executor.async_op;
import cnetmod.coro.cancel;
import cnetmod.coro.timer;
#ifdef CNETMOD_HAS_SSL
import cnetmod.core.ssl;
#endif
import :error;
import :bson_document;
import :connection_options;
import :wire_protocol;
import :scram_sha256;
import :connection;

namespace cnetmod::mongodb {
namespace {
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

    auto binary_payload(const bson_document& response) -> result<std::span<const std::byte>>
    {
        auto value = response.find("payload");
        auto binary = value ? value->get_if<bson_binary>() : nullptr;
        if (!binary || binary->subtype != 0)
            return std::unexpected(make_error(error_code::authentication_failed,
                "MongoDB SASL response has no generic binary payload"));
        return std::span<const std::byte>(binary->bytes);
    }
} // namespace

connection::connection(io_context& context) noexcept : context_(context) {}

connection::~connection()
{
    close();
}

auto connection::connect(connection_options options) -> task<result<void>>
{
    close();
    if (options.host.empty() || options.max_message_bytes < 1024 ||
        options.max_bson_document_bytes < 5 || options.max_bson_document_bytes > options.max_message_bytes)
        co_return std::unexpected(make_error(error_code::connection_failed,
            "invalid MongoDB connection options"));
    options_ = std::move(options);
    happy_eyeballs_options connect_options;
    connect_options.connect_timeout = options_.connect_timeout;
    connect_options.socket_opts.no_delay = true;
    auto connected = co_await async_connect_happy_eyeballs(
        context_, options_.host, options_.port, connect_options);
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
        auto handshake = co_await tls_stream_->async_handshake();
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
        {"driver", bson_document{{"name", "cnetmod"}, {"version", "2.0.0"}}},
        {"os", bson_document{{"type", std::move(operating_system)}}},
        {"application", bson_document{{"name", "cnetmod"}}}};
    bson_document hello{{"hello", std::int32_t{1}}, {"helloOk", true},
        {"client", std::move(client_metadata)}};
    if (options_.enable_zlib_compression)
        hello.append("compression", bson_array{bson_value{"zlib"}});
    auto hello_response = co_await execute_command("admin", std::move(hello));
    if (!hello_response)
    {
        auto failure = hello_response.error();
        close();
        co_return std::unexpected(std::move(failure));
    }
    hello_response_ = *hello_response;
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
    if (options_.enable_zlib_compression)
        if (auto compressors = hello_response->find("compression"); compressors && compressors->as_array())
            for (const auto& candidate : *compressors->as_array())
                if (auto name = candidate.get_if<std::string>(); name && *name == "zlib")
                {
                    selected_compressor_ = compressor_zlib;
                    break;
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
    auto authentication = co_await authenticate();
    if (!authentication)
    {
        auto failure = authentication.error();
        close();
        co_return std::unexpected(std::move(failure));
    }
    authenticated_ = true;
    co_return result<void>{};
}

auto connection::authenticate() -> task<result<void>>
{
    if (options_.username.empty())
        co_return result<void>{};
    scram_sha256_client scram(options_.username, options_.password);
    auto initial = scram.initial_message();
    if (!initial)
        co_return std::unexpected(initial.error());
    bson_document start{{"saslStart", std::int32_t{1}}, {"mechanism", "SCRAM-SHA-256"},
        {"payload", bson_binary{.subtype = 0, .bytes = std::move(*initial)}},
        {"autoAuthorize", std::int32_t{1}},
        {"options", bson_document{{"skipEmptyExchange", true}}}};
    auto first = co_await execute_command(options_.authentication_database, std::move(start));
    if (!first)
        co_return std::unexpected(first.error());
    auto conversation = integer(first->find("conversationId"));
    auto first_payload = binary_payload(*first);
    if (!conversation || !first_payload)
        co_return std::unexpected(first_payload ? make_error(error_code::authentication_failed,
                                                      "MongoDB SASL response has no conversationId")
                                                : first_payload.error());
    auto response = scram.respond(*first_payload);
    if (!response)
    {
        auto failure = response.error();
        close();
        co_return std::unexpected(std::move(failure));
    }
    bson_document continuation{{"saslContinue", std::int32_t{1}},
        {"conversationId", static_cast<std::int32_t>(*conversation)},
        {"payload", bson_binary{.subtype = 0, .bytes = std::move(*response)}}};
    auto second = co_await execute_command(options_.authentication_database, std::move(continuation));
    if (!second)
        co_return std::unexpected(second.error());
    auto second_payload = binary_payload(*second);
    if (!second_payload)
        co_return std::unexpected(second_payload.error());
    auto verified = scram.verify(*second_payload);
    if (!verified)
        co_return std::unexpected(verified.error());
    if (!boolean(second->find("done")).value_or(false))
    {
        bson_document final_continue{{"saslContinue", std::int32_t{1}},
            {"conversationId", static_cast<std::int32_t>(*conversation)},
            {"payload", bson_binary{}}};
        auto final = co_await execute_command(options_.authentication_database, std::move(final_continue));
        if (!final)
            co_return std::unexpected(final.error());
        if (!boolean(final->find("done")).value_or(false))
            co_return std::unexpected(make_error(error_code::authentication_failed,
                "MongoDB SASL exchange did not complete"));
    }
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

auto connection::command(bson_document document) -> task<result<bson_document>>
{
    co_return co_await command(options_.database, std::move(document));
}

auto connection::execute_command_with_timer(std::string database,
    bson_document document, cancel_token& timer_token)
    -> task<result<bson_document>>
{
    auto response = co_await execute_command_without_deadline(
        database, std::move(document));
    timer_token.cancel();
    co_return response;
}

auto connection::command_timeout_watchdog(cancel_token& timer_token,
    std::atomic<bool>& timed_out) -> task<int>
{
    auto waited = co_await async_timer_wait(
        context_, options_.command_timeout, timer_token);
    if (waited)
    {
        timed_out.store(true, std::memory_order_release);
        // Closing only the transport is safe while ssl_stream is executing;
        // full state cleanup happens after the pending I/O unwinds.
        socket_.close();
    }
    co_return 0;
}

auto connection::execute_command(std::string_view database, bson_document document)
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

        ~reset_guard()
        {
            *flag = false;
        }
    } command_guard{&command_in_progress_};

    if (options_.command_timeout <= std::chrono::milliseconds::zero())
    {
        active_command_.store(true, std::memory_order_release);
        command_cancel_requested_.store(false, std::memory_order_release);
        auto response = co_await execute_command_without_deadline(
            database, std::move(document));
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
    active_command_.store(true, std::memory_order_release);
    command_cancel_requested_.store(false, std::memory_order_release);

    auto operation = execute_command_with_timer(
        std::string(database), std::move(document), timer_token);
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
    bson_document document) -> task<result<bson_document>>
{
    if (!connected_ || !socket_.is_open())
        co_return std::unexpected(make_error(error_code::connection_closed,
            "MongoDB socket is closed"));
    document.set("$db", std::string(database));
    if (next_request_id_ <= 0 || next_request_id_ == std::numeric_limits<std::int32_t>::max())
        next_request_id_ = 1;
    auto request_id = next_request_id_++;
    auto encoded = encode_command_message(request_id, document, options_.max_message_bytes);
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
        co_return std::unexpected(std::move(*failure));
    co_return response;
}

void connection::cancel_active_command() noexcept
{
    if (!active_command_.load(std::memory_order_acquire))
        return;
    command_cancel_requested_.store(true, std::memory_order_release);
    socket_.close();
}

auto connection::receive_response(std::int32_t expected) -> task<result<bson_document>>
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
                    destination.size() - position});
        else
            read = co_await async_read(context_, socket_,
                mutable_buffer{destination.data() + position,
                    destination.size() - position});
#else
        auto read = co_await async_read(context_, socket_,
            mutable_buffer{destination.data() + position, destination.size() - position});
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
            const_buffer{source.data(), source.size()});
    else
        written = co_await async_write_all(context_, socket_,
            const_buffer{source.data(), source.size()});
#else
    auto written = co_await async_write_all(context_, socket_, const_buffer{source.data(), source.size()});
#endif
    if (!written)
        co_return std::unexpected(make_error(error_code::connection_closed,
            "MongoDB write failed: " + written.error().message()));
    co_return result<void>{};
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
