module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.http;

import std;
import cnetmod.protocol.http.semantics;
import :request;
import :response;
import :parser;
import :cookie;
import :client;
import cnetmod.core.error;
import cnetmod.core.log;
import cnetmod.core.buffer;
import cnetmod.core.socket;
import cnetmod.core.address;
import cnetmod.core.dns;
import cnetmod.io.io_context;
import cnetmod.protocol.tcp;
import cnetmod.coro.task;
import cnetmod.coro.cancel;
import cnetmod.coro.timer;
import cnetmod.executor.async_op;
import cnetmod.protocol.http.v2.frame;
import cnetmod.protocol.http.v2.settings;
import cnetmod.protocol.http.v2.header_compression;

#ifdef CNETMOD_HAS_SSL
import cnetmod.core.ssl;
    #ifdef CNETMOD_ENABLE_QUIC
import cnetmod.protocol.http.v3.client;
import cnetmod.protocol.http.v3.session;
import cnetmod.protocol.quic;
    #endif
#endif

namespace cnetmod::http {

#if defined(CNETMOD_HAS_SSL) && defined(CNETMOD_ENABLE_QUIC)
namespace {
    auto as_http3_client(const std::shared_ptr<void>& value)
        -> std::shared_ptr<v3::http3_client>
    {
        return std::static_pointer_cast<v3::http3_client>(value);
    }

    auto hex_digit(unsigned value) noexcept -> char
    {
        return value < 10U ? static_cast<char>('0' + value)
                           : static_cast<char>('a' + value - 10U);
    }

    auto encode_ticket(std::span<const std::byte> bytes) -> std::string
    {
        std::string output;
        output.reserve(bytes.size() * 2U);
        for (const auto byte : bytes)
        {
            const auto value = std::to_integer<unsigned>(byte);
            output.push_back(hex_digit(value >> 4U));
            output.push_back(hex_digit(value & 0x0fU));
        }
        return output;
    }

    auto decode_ticket(std::string_view text)
        -> std::optional<std::vector<std::byte>>
    {
        if (text.empty() || (text.size() & 1U) != 0U)
            return std::nullopt;
        std::vector<std::byte> output;
        output.reserve(text.size() / 2U);
        for (std::size_t index = 0; index < text.size(); index += 2U)
        {
            const auto nibble = [](char value) -> int
            {
                if (value >= '0' && value <= '9')
                    return value - '0';
                if (value >= 'a' && value <= 'f')
                    return value - 'a' + 10;
                if (value >= 'A' && value <= 'F')
                    return value - 'A' + 10;
                return -1;
            };
            const int high = nibble(text[index]);
            const int low = nibble(text[index + 1U]);
            if (high < 0 || low < 0)
                return std::nullopt;
            output.push_back(static_cast<std::byte>((high << 4) | low));
        }
        return output;
    }
} // namespace
#endif

// =============================================================================
// SSL Context Initialization
// =============================================================================

void client::load_alt_svc_cache()
{
    if (options_.alt_svc_cache_file.empty())
        return;
    std::ifstream input(options_.alt_svc_cache_file);
    if (!input)
        return;

    const auto now_system = std::chrono::system_clock::now();
    const auto now_steady = std::chrono::steady_clock::now();
    std::string line;
    while (std::getline(input, line))
    {
        const auto first_tab = line.find('\t');
        const auto second_tab = line.find('\t', first_tab == std::string::npos ? first_tab : first_tab + 1);
        if (first_tab == std::string::npos || second_tab == std::string::npos)
            continue;
        const auto third_tab = line.find('\t', second_tab + 1);
        if (third_tab != std::string::npos)
            continue;
        const std::string_view key{line.data(), first_tab};
        const std::string_view peer_text{line.data() + first_tab + 1,
            second_tab - first_tab - 1};
        const std::string_view expiry_text{line.data() + second_tab + 1,
            line.size() - second_tab - 1};
        unsigned peer_port{};
        long long expiry_ms{};
        const auto [peer_end, peer_error] = std::from_chars(
            peer_text.data(), peer_text.data() + peer_text.size(), peer_port);
        const auto [expiry_end, expiry_error] = std::from_chars(
            expiry_text.data(), expiry_text.data() + expiry_text.size(), expiry_ms);
        if (key.empty() || peer_error != std::errc{} ||
            peer_end != peer_text.data() + peer_text.size() || peer_port == 0U ||
            peer_port > 65535U || expiry_error != std::errc{} ||
            expiry_end != expiry_text.data() + expiry_text.size())
            continue;
        const auto expiry_system = std::chrono::system_clock::time_point{
            std::chrono::milliseconds{expiry_ms}};
        if (expiry_system <= now_system)
            continue;
        const auto remaining = expiry_system - now_system;
        h3_alt_svc_[std::string(key)] = {
            now_steady + std::chrono::duration_cast<std::chrono::steady_clock::duration>(remaining),
            static_cast<std::uint16_t>(peer_port)};
    }
}

void client::persist_alt_svc_cache() const
{
    if (options_.alt_svc_cache_file.empty())
        return;
    const auto temporary = options_.alt_svc_cache_file + ".tmp";
    std::ofstream output(temporary, std::ios::trunc);
    if (!output)
        return;
    const auto now_system = std::chrono::system_clock::now();
    const auto now_steady = std::chrono::steady_clock::now();
    for (const auto& [key, entry] : h3_alt_svc_)
    {
        if (entry.expires_at <= now_steady || key.contains('\t'))
            continue;
        const auto remaining = entry.expires_at - now_steady;
        const auto expiry = now_system +
            std::chrono::duration_cast<std::chrono::system_clock::duration>(remaining);
        const auto expiry_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            expiry.time_since_epoch())
                                   .count();
        output << key << '\t' << entry.peer_port << '\t' << expiry_ms << '\n';
    }
    output.close();
    if (!output)
        return;
    std::error_code error;
    std::filesystem::rename(temporary, options_.alt_svc_cache_file, error);
    if (error)
    {
        std::filesystem::remove(options_.alt_svc_cache_file, error);
        error.clear();
        std::filesystem::rename(temporary, options_.alt_svc_cache_file, error);
    }
}

#if defined(CNETMOD_HAS_SSL) && defined(CNETMOD_ENABLE_QUIC)
auto load_http3_resumption_ticket(std::string_view cache_file, std::string_view host,
    std::uint16_t port) -> std::optional<cnetmod::quic::session_ticket>
{
    if (cache_file.empty() || host.empty() || port == 0U ||
        host.find_first_of("\t\r\n") != std::string_view::npos)
        return std::nullopt;
    std::ifstream input{std::string(cache_file)};
    if (!input)
        return std::nullopt;
    std::string line;
    while (std::getline(input, line))
    {
        const auto first_tab = line.find('\t');
        const auto second_tab = line.find('\t', first_tab == std::string::npos ? first_tab : first_tab + 1U);
        if (first_tab == std::string::npos || second_tab == std::string::npos ||
            line.find('\t', second_tab + 1U) != std::string::npos)
            continue;
        const std::string_view line_host{line.data(), first_tab};
        const std::string_view line_port{line.data() + first_tab + 1U,
            second_tab - first_tab - 1U};
        unsigned parsed_port{};
        const auto [end, error] = std::from_chars(line_port.data(),
            line_port.data() + line_port.size(), parsed_port);
        if (line_host != host || error != std::errc{} ||
            end != line_port.data() + line_port.size() || parsed_port != port)
            continue;
        if (const auto bytes = decode_ticket(
                std::string_view{line.data() + second_tab + 1U,
                    line.size() - second_tab - 1U}))
            return cnetmod::quic::session_ticket{std::move(*bytes)};
    }
    return std::nullopt;
}

void persist_http3_resumption_ticket(std::string_view cache_file, std::string_view host,
    std::uint16_t port, const cnetmod::quic::session_ticket& ticket)
{
    if (cache_file.empty() || host.empty() || port == 0U ||
        ticket.empty() || host.find_first_of("\t\r\n") != std::string_view::npos)
        return;

    std::vector<std::string> lines;
    {
        std::ifstream input{std::string(cache_file)};
        std::string line;
        while (input && std::getline(input, line))
        {
            const auto first_tab = line.find('\t');
            const auto second_tab = line.find('\t', first_tab == std::string::npos ? first_tab : first_tab + 1U);
            bool replace = false;
            if (first_tab != std::string::npos && second_tab != std::string::npos &&
                line.find('\t', second_tab + 1U) == std::string::npos)
            {
                unsigned parsed_port{};
                const std::string_view line_port{line.data() + first_tab + 1U,
                    second_tab - first_tab - 1U};
                const auto [end, error] = std::from_chars(line_port.data(),
                    line_port.data() + line_port.size(), parsed_port);
                replace = std::string_view{line.data(), first_tab} == host &&
                    error == std::errc{} && end == line_port.data() + line_port.size() &&
                    parsed_port == port;
            }
            if (!replace && !line.empty())
                lines.push_back(std::move(line));
        }
    }
    lines.push_back(std::string(host) + '\t' + std::to_string(port) + '\t' +
        encode_ticket(ticket.serialized));

    const auto cache_path = std::string(cache_file);
    const auto temporary = cache_path + ".tmp";
    std::ofstream output(temporary, std::ios::trunc);
    if (!output)
        return;
    for (const auto& line : lines)
        output << line << '\n';
    output.close();
    if (!output)
        return;
    std::error_code error;
    std::filesystem::rename(temporary, cache_path, error);
    if (error)
    {
        std::filesystem::remove(cache_path, error);
        error.clear();
        std::filesystem::rename(temporary, cache_path, error);
    }
}
#endif

#ifdef CNETMOD_HAS_SSL
void client::init_ssl_context()
{
    auto ctx_result = ssl_context::client();
    if (!ctx_result)
    {
        return;
    }

    ssl_ctx_ = std::move(*ctx_result);

    if (!options_.ca_file.empty())
    {
        (void)ssl_ctx_->load_ca_file(options_.ca_file);
    }
    else
    {
        (void)ssl_ctx_->set_default_ca();
    }

    if (!options_.cert_file.empty())
    {
        (void)ssl_ctx_->load_cert_file(options_.cert_file);
    }
    if (!options_.key_file.empty())
    {
        (void)ssl_ctx_->load_key_file(options_.key_file);
    }

    ssl_ctx_->set_verify_peer(options_.verify_peer);

    // Configure ALPN based on version preference
    switch (options_.version_pref)
    {
    case http_version_preference::http2_only:
        ssl_ctx_->configure_alpn_client({"h2"});
        break;
    case http_version_preference::http1_only:
        ssl_ctx_->configure_alpn_client({"http/1.1"});
        break;
    case http_version_preference::http2_preferred:
        ssl_ctx_->configure_alpn_client({"h2", "http/1.1"});
        break;
    case http_version_preference::http1_preferred:
        ssl_ctx_->configure_alpn_client({"http/1.1", "h2"});
        break;
    case http_version_preference::http3_only:
    case http_version_preference::http3_preferred:
        // QUIC uses a separate TLS context and ALPN negotiation. Keep the
        // TCP context usable for the explicit fallback policy.
        ssl_ctx_->configure_alpn_client({"h2", "http/1.1"});
        break;
    }

    #ifdef CNETMOD_ENABLE_QUIC
    if (options_.version_pref == http_version_preference::http3_only ||
        options_.version_pref == http_version_preference::http3_preferred ||
        options_.enable_alt_svc_http3)
    {
        auto h3_context = ssl_context::quic_client();
        if (!h3_context)
            return;
        h3_ssl_ctx_ = std::move(*h3_context);
        if (!options_.ca_file.empty())
            (void)h3_ssl_ctx_->load_ca_file(options_.ca_file);
        else
            (void)h3_ssl_ctx_->set_default_ca();
        if (!options_.cert_file.empty())
            (void)h3_ssl_ctx_->load_cert_file(options_.cert_file);
        if (!options_.key_file.empty())
            (void)h3_ssl_ctx_->load_key_file(options_.key_file);
        h3_ssl_ctx_->set_verify_peer(options_.verify_peer);
        h3_ssl_ctx_->configure_alpn_client({"h3"});
    }
    #endif
}
#endif

// =============================================================================
// Connection Management
// =============================================================================

void client::close() noexcept
{
    raced_use_http3_.reset();
#if defined(CNETMOD_HAS_SSL) && defined(CNETMOD_ENABLE_QUIC)
    // Destruction releases the per-origin QUIC connection. `close()` is a
    // legacy synchronous API, so graceful QUIC shutdown is available through
    // the dedicated v3 client while this unified facade remains non-blocking.
    h3_client_.reset();
#endif
    if (!state_)
        return;

#ifdef CNETMOD_HAS_SSL
    if (state_->ssl)
    {
        state_->ssl.reset();
    }
#endif

    if (state_->conn)
    {
        state_->conn->close();
    }

    state_.reset();
}

// =============================================================================
// Async Connect
// =============================================================================

auto client::connect(std::string_view host, std::uint16_t port, bool use_ssl)
    -> task<std::expected<void, std::error_code>>
{
    cancel_token token;
    co_return co_await connect(host, port, use_ssl, token);
}

auto client::close_async() -> task<void>
{
#if defined(CNETMOD_HAS_SSL) && defined(CNETMOD_ENABLE_QUIC)
    co_await h3_lifecycle_mutex_.lock();
    cnetmod::async_lock_guard h3_guard{h3_lifecycle_mutex_, std::adopt_lock};
    if (h3_client_)
    {
        co_await as_http3_client(h3_client_)->close();
        h3_client_.reset();
    }
#endif
    close();
    co_return;
}

auto client::connect(std::string_view host, std::uint16_t port, bool use_ssl,
    cancel_token& token)
    -> task<std::expected<void, std::error_code>>
{
    if (token.is_cancelled())
    {
        co_return std::unexpected(make_error_code(errc::operation_aborted));
    }
    // Reuse connection if same host:port:ssl
    if (state_ && state_->conn && state_->conn->is_open() &&
        state_->host == host && state_->port == port && state_->is_ssl == use_ssl)
    {
        co_return {};
    }

    close();

    state_.emplace();
    state_->host = std::string(host);
    state_->port = port;
    state_->is_ssl = use_ssl;

    auto connect_r = co_await async_connect_happy_eyeballs(
        *ctx_, host, port, {.connect_timeout = options_.connect_timeout}, token);
    if (!connect_r)
    {
        co_return std::unexpected(connect_r.error());
    }

    if (token.is_cancelled())
    {
        co_return std::unexpected(make_error_code(errc::operation_aborted));
    }

    auto sock = std::move(connect_r->sock);

#ifdef CNETMOD_PLATFORM_WINDOWS
    // Keep inline Winsock completions on this coroutine instead of enqueuing
    // an IOCP packet just to resume it.  apply_options is deliberately best
    // effort here: this is a performance capability, not a connection or
    // protocol requirement.
    (void)sock.apply_options(
        {.non_blocking = false, .skip_completion_on_success = true});
#endif

    state_->conn.emplace(*ctx_, std::move(sock));

#ifdef CNETMOD_HAS_SSL
    if (use_ssl)
    {
        if (!ssl_ctx_)
        {
            co_return std::unexpected(make_error_code(std::errc::not_supported));
        }

        // Create SSL stream
        state_->ssl.emplace(*ssl_ctx_, *ctx_, state_->conn->native_socket());
        state_->ssl->set_hostname(host);
        state_->ssl->set_connect_state();

        // Async SSL handshake
        auto handshake_result = co_await state_->ssl->async_handshake(token);
        if (!handshake_result)
        {
            close();
            co_return std::unexpected(handshake_result.error());
        }

        // Check ALPN negotiation
        auto alpn = state_->ssl->get_alpn_selected();
        if (alpn == "h2")
        {
            state_->protocol = protocol_type::http2;
        }
        else
        {
            state_->protocol = protocol_type::http1;
        }
    }
    else
#endif
    {
        // Plain HTTP - check version preference
        switch (options_.version_pref)
        {
        case http_version_preference::http2_only:
        {
            state_->protocol = protocol_type::http2;
            break;
        }
        default:
            state_->protocol = protocol_type::http1;
            break;
        }
    }

    co_return {};
}

// =============================================================================
// Async I/O Helpers
// =============================================================================

auto client::write_data(std::string_view data)
    -> task<std::expected<void, std::error_code>>
{
    cancel_token token;
    co_return co_await write_data(data, token);
}

auto client::write_data(std::string_view data, cancel_token& token)
    -> task<std::expected<void, std::error_code>>
{
    if (!state_ || !state_->conn || !state_->conn->is_open())
    {
        co_return std::unexpected(make_error_code(std::errc::not_connected));
    }

#ifdef CNETMOD_HAS_SSL
    if (state_->is_ssl && state_->ssl)
    {
        co_return co_await state_->ssl->async_write_all(
            const_buffer{data.data(), data.size()}, token);
    }
#endif

    auto& sock = state_->conn->native_socket();
    co_return co_await async_write_all(*ctx_, sock,
        const_buffer{data.data(), data.size()}, token);
}

auto client::read_data(void* buffer, std::size_t size)
    -> task<std::expected<std::size_t, std::error_code>>
{
    cancel_token token;
    co_return co_await read_data(buffer, size, token);
}

auto client::read_data(void* buffer, std::size_t size, cancel_token& token)
    -> task<std::expected<std::size_t, std::error_code>>
{
    if (!state_ || !state_->conn || !state_->conn->is_open())
    {
        co_return std::unexpected(make_error_code(std::errc::not_connected));
    }

#ifdef CNETMOD_HAS_SSL
    if (state_->is_ssl && state_->ssl)
    {
        co_return co_await state_->ssl->async_read(mutable_buffer{buffer, size}, token);
    }
#endif

    auto& sock = state_->conn->native_socket();
    co_return co_await async_read(*ctx_, sock, mutable_buffer{buffer, size}, token);
}

// =============================================================================
// HTTP/1.1 Implementation
// =============================================================================

auto client::send_http1(const request& req)
    -> task<std::expected<response, std::error_code>>
{
    cancel_token token;
    co_return co_await send_http1(req, token);
}

auto client::send_http1(const request& req, cancel_token& token)
    -> task<std::expected<response, std::error_code>>
{
    if (options_.http1_response_body_limit != 0)
        return send_http1_impl<true>(req, token);
    return send_http1_impl<false>(req, token);
}

template <bool Bounded>
auto client::send_http1_impl(const request& req, cancel_token& token)
    -> task<std::expected<response, std::error_code>>
{
    if (!state_)
    {
        co_return std::unexpected(make_error_code(std::errc::not_connected));
    }

    // Extract path from URI
    auto uri = req.uri();
    std::string path = "/";

    if (uri.starts_with("http://") || uri.starts_with("https://"))
    {
        auto url_result = url::parse(uri);
        if (!url_result)
        {
            co_return std::unexpected(make_error_code(http_errc::invalid_uri));
        }
        path = url_result->path;
        if (!url_result->query.empty())
        {
            path += "?";
            path += url_result->query;
        }
    }
    else
    {
        path = std::string(uri);
    }

    std::string cookie_header;
    if (options_.enable_cookies && req.get_header("Cookie").empty())
    {
        auto generated_cookie = cookies_.to_cookie_header(
            state_->host, path, state_->is_ssl);
        if (!generated_cookie.empty())
            cookie_header = std::move(generated_cookie);
    }

    // Serialize directly instead of copying request and mutating its header map.
    auto& request_data = state_->request_buffer;
    request_data.clear();
    request_data.reserve(256 + path.size() + req.body().size());
    request_data += method_to_string(req.method());
    request_data += ' ';
    request_data += path;
    request_data += ' ';
    request_data += version_to_string(req.version());
    request_data += "\r\n";

    bool has_content_length = false;
    bool has_transfer_encoding = false;
    for (const auto& [key, value] : req.headers())
    {
        request_data += key;
        request_data += ": ";
        request_data += value;
        request_data += "\r\n";
        std::string lower_key;
        lower_key.reserve(key.size());
        for (const auto ch : key)
            lower_key.push_back(static_cast<char>(std::tolower(
                static_cast<unsigned char>(ch))));
        has_content_length |= lower_key == "content-length";
        has_transfer_encoding |= lower_key == "transfer-encoding";
    }

    if (req.get_header("Host").empty())
    {
        request_data += "Host: ";
        request_data += format_authority(state_->host, state_->port, state_->is_ssl);
        request_data += "\r\n";
    }

    if (req.get_header("User-Agent").empty())
    {
        request_data += "User-Agent: ";
        request_data += options_.user_agent;
        request_data += "\r\n";
    }

    if (req.get_header("Connection").empty())
    {
        request_data += "Connection: ";
        request_data += options_.keep_alive ? "keep-alive" : "close";
        request_data += "\r\n";
    }

    if (!cookie_header.empty())
    {
        request_data += "Cookie: ";
        request_data += cookie_header;
        request_data += "\r\n";
    }

    if (const auto& source = req.body_source(); source &&
        ((has_content_length && !source->content_length()) ||
            (has_content_length && has_transfer_encoding)))
    {
        co_return std::unexpected(make_error_code(std::errc::invalid_argument));
    }

    // A pull body without a declared length uses RFC 9112 chunked framing.
    // Never emit both framing headers, and preserve an explicitly supplied
    // length so callers can stream a known-size upload without buffering it.
    if (req.has_streaming_body() && !has_content_length &&
        !has_transfer_encoding)
    {
        request_data += "Transfer-Encoding: chunked\r\n";
        has_transfer_encoding = true;
    }

    request_data += "\r\n";
    if (!req.has_streaming_body() && !req.body().empty())
    {
        request_data += req.body();
    }

    // Send request
    auto send_result = co_await write_data(request_data, token);
    if (!send_result)
    {
        co_return std::unexpected(send_result.error());
    }

    if (const auto& source = req.body_source())
    {
        const auto expected_length = source->content_length();
        std::uint64_t sent_length = 0;
        for (;;)
        {
            if (token.is_cancelled())
            {
                close();
                co_return std::unexpected(make_error_code(errc::operation_aborted));
            }
            auto next = co_await source->next(token);
            if (!next)
            {
                if (token.is_cancelled())
                {
                    close();
                    co_return std::unexpected(make_error_code(errc::operation_aborted));
                }
                break;
            }
            const auto chunk = next->view();
            if (chunk.empty())
                continue;
            if (expected_length &&
                (sent_length > *expected_length ||
                    chunk.size() > *expected_length - sent_length))
            {
                close();
                co_return std::unexpected(make_error_code(http_errc::body_too_large));
            }

            if (has_transfer_encoding)
            {
                const auto prefix = std::format("{:X}\r\n", chunk.size());
                auto prefix_result = co_await write_data(prefix, token);
                if (!prefix_result)
                    co_return std::unexpected(prefix_result.error());
            }
            auto data_result = co_await write_data(
                {reinterpret_cast<const char*>(chunk.data()), chunk.size()}, token);
            if (!data_result)
                co_return std::unexpected(data_result.error());
            if (has_transfer_encoding)
            {
                auto suffix_result = co_await write_data("\r\n", token);
                if (!suffix_result)
                    co_return std::unexpected(suffix_result.error());
            }
            sent_length += chunk.size();
        }
        if (expected_length && sent_length != *expected_length)
        {
            close();
            co_return std::unexpected(make_error_code(http_errc::incomplete_message));
        }
        if (has_transfer_encoding)
        {
            auto end_result = co_await write_data("0\r\n\r\n", token);
            if (!end_result)
                co_return std::unexpected(end_result.error());
        }
    }

    // Receive response - read until we have complete headers
    auto& buffer = state_->read_buffer;
    buffer.clear();
    buffer.reserve(4096);
    std::size_t header_end = std::string::npos;

    while (header_end == std::string::npos)
    {
        char temp[4096];
        auto result = co_await read_data(temp, sizeof(temp), token);
        if (!result)
        {
            co_return std::unexpected(result.error());
        }

        if (*result == 0)
        {
            co_return std::unexpected(make_error_code(std::errc::connection_reset));
        }

        buffer.append(temp, *result);
        header_end = buffer.find("\r\n\r\n");

        if (buffer.size() > max_header_size)
        {
            co_return std::unexpected(make_error_code(http_errc::header_too_large));
        }
    }

    // Parse status line and headers
    response resp;
    std::string_view view = buffer;

    // Parse status line
    auto line_end = view.find("\r\n");
    if (line_end == std::string_view::npos)
    {
        co_return std::unexpected(make_error_code(http_errc::invalid_status_line));
    }

    auto status_line = view.substr(0, line_end);
    view = view.substr(line_end + 2);

    // Parse "HTTP/1.1 200 OK"
    auto space1 = status_line.find(' ');
    if (space1 == std::string_view::npos)
    {
        co_return std::unexpected(make_error_code(http_errc::invalid_status_line));
    }

    auto version_str = status_line.substr(0, space1);
    auto version_opt = string_to_version(version_str);
    if (!version_opt)
    {
        co_return std::unexpected(make_error_code(http_errc::invalid_version));
    }
    resp.set_version(*version_opt);

    auto rest = status_line.substr(space1 + 1);
    auto space2 = rest.find(' ');

    int status_code = 0;
    auto status_str = (space2 != std::string_view::npos)
        ? rest.substr(0, space2)
        : rest;

    auto [ptr, ec] = std::from_chars(status_str.data(),
        status_str.data() + status_str.size(),
        status_code);
    if (ec != std::errc{})
    {
        co_return std::unexpected(make_error_code(http_errc::invalid_status_line));
    }
    resp.set_status(status_code);

    if (space2 != std::string_view::npos)
    {
        resp.set_status_message(rest.substr(space2 + 1));
    }

    // Parse headers
    while (true)
    {
        line_end = view.find("\r\n");
        if (line_end == std::string_view::npos)
        {
            break;
        }

        auto line = view.substr(0, line_end);
        if (line.empty())
        {
            view = view.substr(2);
            break;
        }

        auto colon = line.find(':');
        if (colon == std::string_view::npos)
        {
            co_return std::unexpected(make_error_code(http_errc::invalid_header));
        }

        auto key = line.substr(0, colon);
        auto value = line.substr(colon + 1);

        // Trim whitespace
        while (!value.empty() && value[0] == ' ')
            value = value.substr(1);
        while (!value.empty() && value.back() == ' ')
            value = value.substr(0, value.size() - 1);

        resp.set_header(key, value);
        view = view.substr(line_end + 2);
    }

    // Read body
    auto& body = state_->body_buffer;
    body.clear();
    auto content_length_str = resp.get_header("Content-Length");
    const auto body_limit = Bounded ? options_.http1_response_body_limit : std::size_t{0};
    bool close_delimited = false;

    if (!content_length_str.empty())
    {
        std::size_t content_length = 0;
        auto [ptr2, ec2] = std::from_chars(content_length_str.data(),
            content_length_str.data() +
                content_length_str.size(),
            content_length);
        if (body_limit != 0 && (ec2 != std::errc{} || ptr2 != content_length_str.data() + content_length_str.size()))
            co_return std::unexpected(make_error_code(http_errc::invalid_header));
        if (ec2 == std::errc{})
        {
            if (content_length > max_body_size || (body_limit != 0 && content_length > body_limit))
            {
                co_return std::unexpected(make_error_code(http_errc::body_too_large));
            }

            body.reserve(content_length);

            // Append any body data already in buffer
            if (!view.empty())
            {
                body.append(body_limit != 0 ? view.substr(0, content_length) : view);
            }

            // Read remaining body
            while (body.size() < content_length)
            {
                char temp[4096];
                auto to_read = std::min(sizeof(temp), content_length - body.size());
                auto result = co_await read_data(temp, to_read, token);
                if (!result)
                {
                    if (body_limit != 0 && result.error() == cnetmod::make_error_code(errc::end_of_file))
                        co_return std::unexpected(make_error_code(http_errc::invalid_header));
                    co_return std::unexpected(result.error());
                }
                if (*result == 0)
                {
                    if (body_limit != 0)
                        co_return std::unexpected(make_error_code(http_errc::invalid_header));
                    break;
                }
                body.append(temp, *result);
            }
        }
    }
    else if (resp.get_header("Transfer-Encoding").find("chunked") !=
        std::string_view::npos)
    {
        // Handle chunked encoding - complete
        std::string remaining_data(view);

        while (true)
        {
            // Chunk size
            auto crlf_pos = remaining_data.find("\r\n");

            // Complete, read
            while (crlf_pos == std::string::npos)
            {
                if (body_limit != 0 && remaining_data.size() > max_header_size)
                    co_return std::unexpected(make_error_code(http_errc::header_too_large));
                char temp[4096];
                auto result = co_await read_data(temp, sizeof(temp), token);
                if (body_limit != 0 && ((!result && result.error() == cnetmod::make_error_code(errc::end_of_file)) || (result && *result == 0)))
                    co_return std::unexpected(make_error_code(http_errc::invalid_chunk));
                if (!result)
                {
                    co_return std::unexpected(result.error());
                }
                if (*result == 0)
                {
                    co_return std::unexpected(make_error_code(std::errc::connection_reset));
                }
                remaining_data.append(temp, *result);
                crlf_pos = remaining_data.find("\r\n");
            }

            if (body_limit != 0 && crlf_pos > max_header_size)
                co_return std::unexpected(make_error_code(http_errc::header_too_large));

            // Parse chunk size ()
            auto size_str = remaining_data.substr(0, crlf_pos);

            // Implementation note.
            auto semicolon = size_str.find(';');
            if (semicolon != std::string::npos)
            {
                size_str = size_str.substr(0, semicolon);
            }

            std::size_t chunk_size = 0;
            auto [ptr, ec] = std::from_chars(
                size_str.data(),
                size_str.data() + size_str.size(),
                chunk_size, 16); // Implementation note.

            if (ec != std::errc{} || (body_limit != 0 && ptr != size_str.data() + size_str.size()))
            {
                co_return std::unexpected(make_error_code(http_errc::invalid_chunk));
            }

            if (body_limit != 0 && (chunk_size > body_limit - body.size() || chunk_size > std::numeric_limits<std::size_t>::max() - 2U))
                co_return std::unexpected(make_error_code(http_errc::body_too_large));

            // Chunk size 0
            if (chunk_size == 0)
            {
                // Read trailer headers()
                remaining_data = remaining_data.substr(crlf_pos + 2);
                std::size_t trailer_bytes{};

                // Implementation note: trailer.
                while (true)
                {
                    if (body_limit != 0 && remaining_data.size() > max_header_size - trailer_bytes)
                        co_return std::unexpected(make_error_code(http_errc::header_too_large));
                    auto trailer_crlf = remaining_data.find("\r\n");
                    if (trailer_crlf == std::string::npos)
                    {
                        char temp[4096];
                        auto result = co_await read_data(temp, sizeof(temp), token);
                        if (!result || *result == 0)
                        {
                            if (body_limit != 0)
                                co_return std::unexpected(result || result.error() == cnetmod::make_error_code(errc::end_of_file)
                                        ? make_error_code(http_errc::invalid_chunk)
                                        : result.error());
                            break;
                        }
                        remaining_data.append(temp, *result);
                        continue;
                    }

                    if (trailer_crlf == 0)
                    {
                        // Implementation note.
                        break;
                    }

                    // Implementation note.
                    trailer_bytes += trailer_crlf + 2;
                    remaining_data = remaining_data.substr(trailer_crlf + 2);
                }

                break; // Chunks read
            }

            // Implementation note: size.
            remaining_data = remaining_data.substr(crlf_pos + 2);

            // (chunk data + \r\n)
            while (remaining_data.size() < chunk_size + 2)
            {
                char temp[4096];
                auto result = co_await read_data(temp, sizeof(temp), token);
                if (body_limit != 0 && ((!result && result.error() == cnetmod::make_error_code(errc::end_of_file)) || (result && *result == 0)))
                    co_return std::unexpected(make_error_code(http_errc::invalid_chunk));
                if (!result)
                {
                    co_return std::unexpected(result.error());
                }
                if (*result == 0)
                {
                    co_return std::unexpected(make_error_code(std::errc::connection_reset));
                }
                remaining_data.append(temp, *result);
            }

            // Implementation note: chunk.
            if (body_limit != 0 && remaining_data.compare(chunk_size, 2, "\r\n") != 0)
                co_return std::unexpected(make_error_code(http_errc::invalid_chunk));
            body.append(remaining_data.substr(0, chunk_size));

            // Chunk \r\n
            remaining_data = remaining_data.substr(chunk_size + 2);
        }
    }

    else if (body_limit != 0 && req.method() != http_method::HEAD &&
        resp.status_code() >= 200 && resp.status_code() != 204 && resp.status_code() != 304 &&
        !(req.method() == http_method::CONNECT && resp.status_code() < 300))
    {
        if (!resp.get_header("Transfer-Encoding").empty())
            co_return std::unexpected(make_error_code(http_errc::invalid_header));
        close_delimited = true;
        if (view.size() > body_limit)
            co_return std::unexpected(make_error_code(http_errc::body_too_large));
        body.append(view);
        for (;;)
        {
            char bytes[4096];
            const auto remaining = body_limit - body.size();
            const auto capacity = remaining == 0 ? std::size_t{1} : std::min(sizeof(bytes), remaining);
            const auto received = co_await read_data(bytes, capacity, token);
            if (!received)
            {
                if (received.error() == cnetmod::make_error_code(errc::end_of_file))
                    break;
                co_return std::unexpected(received.error());
            }
            if (*received == 0)
                break;
            if (*received > remaining)
                co_return std::unexpected(make_error_code(http_errc::body_too_large));
            body.append(bytes, *received);
        }
    }

    resp.set_body_preserve_headers(std::move(body));

    // Set-Cookie
    if (options_.enable_cookies)
    {
        for (const auto& [key, value] : resp.headers())
        {
            if (key == "Set-Cookie" || key == "set-cookie")
            {
                cookies_.add_from_header(value);
            }
        }
    }

    // Handle Connection header
    auto connection = resp.get_header("Connection");
    if (close_delimited || !options_.keep_alive || connection == "close")
    {
        close();
    }

    co_return resp;
}

// =============================================================================
// HTTP/2 Client (cleartext h2c prior-knowledge)
// =============================================================================

auto client::send_http2(const request& req)
    -> task<std::expected<response, std::error_code>>
{
    cancel_token token;
    co_return co_await send_http2(req, token);
}

auto client::send_http2(const request& req, cancel_token& token)
    -> task<std::expected<response, std::error_code>>
{
    if (!state_ || !state_->conn)
    {
        co_return std::unexpected(make_error_code(std::errc::not_connected));
    }
    auto append_frame = [](std::vector<std::byte>& output, v2::frame_header header,
                            std::span<const std::byte> payload)
    {
        header.length = static_cast<std::uint32_t>(payload.size());
        const auto encoded = v2::encode_frame_header(header);
        output.insert(output.end(), encoded.begin(), encoded.end());
        output.insert(output.end(), payload.begin(), payload.end());
    };
    std::vector<std::byte> outbound;
    if (!state_->h2_initialized)
    {
        constexpr std::string_view preface = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
        for (const auto ch : preface)
            outbound.push_back(static_cast<std::byte>(ch));
        const v2::settings settings{.max_concurrent_streams = options_.h2_max_concurrent_streams,
            .initial_window_size = options_.h2_initial_window_size};
        const auto values = v2::encode_settings(settings);
        append_frame(outbound, {.type = v2::frame_type::settings}, values);
        state_->h2_initialized = true;
    }

    const auto stream_id = state_->h2_next_stream_id;
    if (stream_id == 0 || stream_id > 0x7fff'fffdU)
    {
        co_return std::unexpected(make_error_code(std::errc::resource_unavailable_try_again));
    }
    state_->h2_next_stream_id += 2;
    std::string authority = std::string(req.get_header("Host"));
    if (authority.empty())
        authority = format_authority(state_->host, state_->port, state_->is_ssl);
    std::string target(req.uri());
    if (target.starts_with("http://") || target.starts_with("https://"))
    {
        auto parsed = url::parse(target);
        if (!parsed)
            co_return std::unexpected(make_error_code(http_errc::invalid_uri));
        target = parsed->path;
        if (!parsed->query.empty())
        {
            target.push_back('?');
            target += parsed->query;
        }
    }
    std::array<v2::header_field, 4> pseudo{{
        {":method", std::string(method_to_string(req.method()))},
        {":scheme", state_->is_ssl ? "https" : "http"},
        {":authority", std::move(authority)},
        {":path", std::move(target)},
    }};
    std::vector<v2::header_field> fields(pseudo.begin(), pseudo.end());
    for (const auto& [name, value] : req.headers())
    {
        std::string lowercase;
        lowercase.reserve(name.size());
        for (const auto ch : name)
            lowercase.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        if (lowercase == "host" || lowercase == "connection" || lowercase == "transfer-encoding" ||
            lowercase == "upgrade" || lowercase == "keep-alive")
            continue;
        fields.push_back({std::move(lowercase), value,
            lowercase == "authorization" || lowercase == "cookie"});
    }
    const auto block = state_->h2_encoder.encode(fields);
    if (!block || block->size() > 16 * 1024)
    {
        co_return std::unexpected(block ? make_error_code(std::errc::message_size) : block.error());
    }
    const bool has_streaming_body = req.has_streaming_body();
    const bool has_body = has_streaming_body || !req.body().empty();
    append_frame(outbound, {.type = v2::frame_type::headers, .flags = static_cast<std::uint8_t>(0x4 | (has_body ? 0 : 0x1)), .stream_id = stream_id}, *block);
    if (has_body && !has_streaming_body)
    {
        const auto body = req.body();
        append_frame(outbound, {.type = v2::frame_type::data, .flags = 0x1, .stream_id = stream_id},
            {reinterpret_cast<const std::byte*>(body.data()), body.size()});
    }
    const auto write = co_await write_data({reinterpret_cast<const char*>(outbound.data()), outbound.size()}, token);
    if (!write)
        co_return std::unexpected(write.error());

    if (const auto& source = req.body_source())
    {
        const auto expected_length = source->content_length();
        std::uint64_t sent_length = 0;
        for (;;)
        {
            if (token.is_cancelled())
            {
                close();
                co_return std::unexpected(make_error_code(errc::operation_aborted));
            }
            auto next = co_await source->next(token);
            if (!next)
            {
                if (token.is_cancelled())
                {
                    close();
                    co_return std::unexpected(make_error_code(errc::operation_aborted));
                }
                break;
            }
            const auto chunk = next->view();
            if (chunk.empty())
                continue;
            if (expected_length &&
                (sent_length > *expected_length ||
                    chunk.size() > *expected_length - sent_length))
            {
                close();
                co_return std::unexpected(make_error_code(http_errc::body_too_large));
            }

            // DATA payloads are kept below the default peer frame size.  The
            // write is awaited before asking the producer for more data,
            // providing bounded memory and socket-level back-pressure.
            constexpr std::size_t max_data_payload = 16 * 1024;
            for (std::size_t offset = 0; offset < chunk.size();)
            {
                const auto count = std::min(max_data_payload, chunk.size() - offset);
                std::vector<std::byte> frame;
                append_frame(frame,
                    {.type = v2::frame_type::data, .stream_id = stream_id},
                    chunk.subspan(offset, count));
                const auto data_write = co_await write_data(
                    {reinterpret_cast<const char*>(frame.data()), frame.size()}, token);
                if (!data_write)
                    co_return std::unexpected(data_write.error());
                offset += count;
            }
            sent_length += chunk.size();
        }
        if (expected_length && sent_length != *expected_length)
        {
            close();
            co_return std::unexpected(make_error_code(http_errc::incomplete_message));
        }

        // A streaming request has no END_STREAM on HEADERS.  Close it with an
        // empty DATA frame once the producer reaches EOF.
        std::vector<std::byte> end_frame;
        append_frame(end_frame,
            {.type = v2::frame_type::data, .flags = 0x1, .stream_id = stream_id},
            {});
        const auto end_write = co_await write_data(
            {reinterpret_cast<const char*>(end_frame.data()), end_frame.size()}, token);
        if (!end_write)
            co_return std::unexpected(end_write.error());
    }

    response result(200, http_version::http_2);
    bool received_headers = false;
    bool completed = false;
    std::vector<std::byte> input;
    input.reserve(16 * 1024);
    // read_data writes the exact range subsequently appended to input.  This
    // request-local scratch space must not clear 16 KiB before every HTTP/2
    // response, particularly when a one-frame response is read per request.
    std::array<std::byte, 16 * 1024> buffer;
    while (!completed)
    {
        while (input.size() < v2::frame_header_size)
        {
            const auto read = co_await read_data(buffer.data(), buffer.size(), token);
            if (!read || *read == 0)
                co_return std::unexpected(read ? make_error_code(std::errc::connection_reset) : read.error());
            input.insert(input.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(*read));
        }
        const auto header = v2::decode_frame_header({input.data(), v2::frame_header_size});
        if (!header || header->length > 16 * 1024 * 1024)
            co_return std::unexpected(header ? make_error_code(std::errc::message_size) : header.error());
        const auto required = v2::frame_header_size + static_cast<std::size_t>(header->length);
        while (input.size() < required)
        {
            const auto read = co_await read_data(buffer.data(), buffer.size(), token);
            if (!read || *read == 0)
                co_return std::unexpected(read ? make_error_code(std::errc::connection_reset) : read.error());
            input.insert(input.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(*read));
        }
        const auto payload = std::span{input.data() + v2::frame_header_size, static_cast<std::size_t>(header->length)};
        if (header->type == v2::frame_type::settings && (header->flags & 0x1) == 0)
        {
            v2::settings peer;
            if (const auto settings_error = v2::decode_settings(payload, peer); settings_error)
                co_return std::unexpected(settings_error);
            state_->h2_encoder.set_dynamic_table_limit(peer.header_table_size);
            const std::array<std::byte, 0> empty{};
            std::vector<std::byte> ack;
            append_frame(ack, {.type = v2::frame_type::settings, .flags = 0x1}, empty);
            const auto ack_write = co_await write_data({reinterpret_cast<const char*>(ack.data()), ack.size()}, token);
            if (!ack_write)
                co_return std::unexpected(ack_write.error());
        }
        else if (header->type == v2::frame_type::headers && header->stream_id == stream_id)
        {
            const auto decoded = state_->h2_decoder.decode(payload);
            if (!decoded)
                co_return std::unexpected(decoded.error());
            for (const auto& field : *decoded)
            {
                if (field.name == ":status")
                {
                    int status{};
                    const auto [end, ec] = std::from_chars(field.value.data(), field.value.data() + field.value.size(), status);
                    if (ec != std::errc{} || end != field.value.data() + field.value.size())
                        co_return std::unexpected(make_error_code(std::errc::protocol_error));
                    result.set_status(status);
                }
                else if (!field.name.starts_with(':'))
                {
                    result.append_header(field.name, field.value);
                }
            }
            received_headers = true;
            completed = (header->flags & 0x1) != 0;
        }
        else if (header->type == v2::frame_type::data && header->stream_id == stream_id)
        {
            if (!received_headers)
                co_return std::unexpected(make_error_code(std::errc::protocol_error));
            result.set_body_preserve_headers(result.take_body() + std::string(reinterpret_cast<const char*>(payload.data()), payload.size()));
            completed = (header->flags & 0x1) != 0;
        }
        else if (header->type == v2::frame_type::rst_stream && header->stream_id == stream_id)
        {
            co_return std::unexpected(make_error_code(std::errc::connection_aborted));
        }
        else if (header->type == v2::frame_type::goaway)
        {
            co_return std::unexpected(make_error_code(std::errc::connection_aborted));
        }
        input.erase(input.begin(), input.begin() + static_cast<std::ptrdiff_t>(required));
    }
    co_return result;
}

auto client::send_http2_batch(std::span<const request> requests)
    -> task<std::vector<std::expected<response, std::error_code>>>
{
    std::vector<std::expected<response, std::error_code>> results;
    results.reserve(requests.size());
    for (std::size_t i = 0; i < requests.size(); ++i)
    {
        results.emplace_back(std::unexpected(make_error_code(std::errc::operation_canceled)));
    }
    if (requests.empty())
        co_return results;
    if (!state_ || !state_->conn)
        co_return results;

    auto append_frame = [](std::vector<std::byte>& output, v2::frame_header header,
                            std::span<const std::byte> payload)
    {
        header.length = static_cast<std::uint32_t>(payload.size());
        const auto encoded = v2::encode_frame_header(header);
        output.insert(output.end(), encoded.begin(), encoded.end());
        output.insert(output.end(), payload.begin(), payload.end());
    };
    auto request_target = [](const request& request) -> std::expected<std::string, std::error_code>
    {
        std::string target(request.uri());
        if (!target.starts_with("http://") && !target.starts_with("https://"))
            return target;
        const auto parsed = url::parse(target);
        if (!parsed)
            return std::unexpected(make_error_code(http_errc::invalid_uri));
        target = parsed->path;
        if (!parsed->query.empty())
        {
            target.push_back('?');
            target += parsed->query;
        }
        return target;
    };

    std::vector<std::byte> outbound;
    if (!state_->h2_initialized)
    {
        constexpr std::string_view preface = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
        outbound.reserve(preface.size() + requests.size() * 128);
        for (const auto ch : preface)
            outbound.push_back(static_cast<std::byte>(ch));
        const v2::settings settings{.max_concurrent_streams = options_.h2_max_concurrent_streams,
            .initial_window_size = options_.h2_initial_window_size};
        const auto values = v2::encode_settings(settings);
        append_frame(outbound, {.type = v2::frame_type::settings}, values);
        state_->h2_initialized = true;
    }

    std::unordered_map<std::uint32_t, std::size_t> stream_indexes;
    stream_indexes.reserve(requests.size());
    std::vector<bool> received_headers(requests.size(), false);
    std::vector<bool> completed(requests.size(), false);

    for (std::size_t index = 0; index < requests.size(); ++index)
    {
        const auto& req = requests[index];
        const auto stream_id = state_->h2_next_stream_id;
        if (stream_id == 0 || stream_id > 0x7fff'fffdU)
            co_return results;
        state_->h2_next_stream_id += 2;
        auto target = request_target(req);
        if (!target)
            co_return results;
        std::string authority(req.get_header("Host"));
        if (authority.empty())
            authority = format_authority(state_->host, state_->port, state_->is_ssl);
        std::vector<v2::header_field> fields{
            {":method", std::string(method_to_string(req.method()))},
            {":scheme", state_->is_ssl ? "https" : "http"},
            {":authority", std::move(authority)},
            {":path", std::move(*target)},
        };
        fields.reserve(fields.size() + req.headers().size());
        for (const auto& [name, value] : req.headers())
        {
            std::string lowercase;
            lowercase.reserve(name.size());
            for (const auto ch : name)
            {
                lowercase.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
            }
            if (lowercase == "host" || lowercase == "connection" || lowercase == "transfer-encoding" ||
                lowercase == "upgrade" || lowercase == "keep-alive")
                continue;
            const bool sensitive = lowercase == "authorization" || lowercase == "cookie";
            fields.push_back({std::move(lowercase), value, sensitive});
        }
        const auto block = state_->h2_encoder.encode(fields);
        if (!block || block->size() > 16 * 1024)
            co_return results;
        const bool has_body = !req.body().empty();
        append_frame(outbound, {.type = v2::frame_type::headers, .flags = static_cast<std::uint8_t>(0x4 | (has_body ? 0 : 0x1)), .stream_id = stream_id}, *block);
        const auto body = req.body();
        for (std::size_t offset = 0; offset < body.size();)
        {
            const auto size = std::min<std::size_t>(16 * 1024, body.size() - offset);
            const bool final = offset + size == body.size();
            append_frame(outbound, {.type = v2::frame_type::data, .flags = static_cast<std::uint8_t>(final ? 0x1 : 0), .stream_id = stream_id},
                {reinterpret_cast<const std::byte*>(body.data() + offset), size});
            offset += size;
        }
        stream_indexes.emplace(stream_id, index);
    }

    const auto write = co_await write_data({reinterpret_cast<const char*>(outbound.data()), outbound.size()});
    if (!write)
    {
        for (auto& result : results)
            result = std::unexpected(write.error());
        co_return results;
    }

    std::size_t pending = requests.size();
    std::vector<std::byte> input;
    input.reserve(16 * 1024);
    // Only the byte count returned by read_data is consumed below.
    std::array<std::byte, 16 * 1024> buffer;
    while (pending != 0)
    {
        while (input.size() < v2::frame_header_size)
        {
            const auto read = co_await read_data(buffer.data(), buffer.size());
            if (!read || *read == 0)
            {
                const auto error = read ? make_error_code(std::errc::connection_reset) : read.error();
                for (std::size_t i = 0; i < results.size(); ++i)
                    if (!completed[i])
                        results[i] = std::unexpected(error);
                co_return results;
            }
            input.insert(input.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(*read));
        }
        const auto header = v2::decode_frame_header({input.data(), v2::frame_header_size});
        if (!header || header->length > 16 * 1024 * 1024)
            co_return results;
        const auto required = v2::frame_header_size + static_cast<std::size_t>(header->length);
        while (input.size() < required)
        {
            const auto read = co_await read_data(buffer.data(), buffer.size());
            if (!read || *read == 0)
                co_return results;
            input.insert(input.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(*read));
        }
        const auto payload = std::span{input.data() + v2::frame_header_size,
            static_cast<std::size_t>(header->length)};
        if (header->type == v2::frame_type::settings && (header->flags & 0x1) == 0)
        {
            v2::settings peer;
            if (const auto error = v2::decode_settings(payload, peer); error)
                co_return results;
            state_->h2_encoder.set_dynamic_table_limit(peer.header_table_size);
            std::vector<std::byte> ack;
            append_frame(ack, {.type = v2::frame_type::settings, .flags = 0x1}, {});
            const auto ack_write = co_await write_data({reinterpret_cast<const char*>(ack.data()), ack.size()});
            if (!ack_write)
                co_return results;
        }
        else if (header->type == v2::frame_type::ping && (header->flags & 0x1) == 0)
        {
            std::vector<std::byte> ack;
            append_frame(ack, {.type = v2::frame_type::ping, .flags = 0x1}, payload);
            const auto ack_write = co_await write_data({reinterpret_cast<const char*>(ack.data()), ack.size()});
            if (!ack_write)
                co_return results;
        }
        else if (const auto found = stream_indexes.find(header->stream_id); found != stream_indexes.end())
        {
            const auto index = found->second;
            if (header->type == v2::frame_type::headers)
            {
                const auto decoded = state_->h2_decoder.decode(payload);
                if (!decoded)
                    co_return results;
                response current(200, http_version::http_2);
                if (received_headers[index])
                    current = std::move(*results[index]);
                for (const auto& field : *decoded)
                {
                    if (field.name == ":status")
                    {
                        int status{};
                        const auto [end, error] = std::from_chars(field.value.data(), field.value.data() + field.value.size(), status);
                        if (error != std::errc{} || end != field.value.data() + field.value.size())
                            co_return results;
                        current.set_status(status);
                    }
                    else if (!field.name.starts_with(':'))
                    {
                        current.append_header(field.name, field.value);
                    }
                }
                results[index] = std::move(current);
                received_headers[index] = true;
            }
            else if (header->type == v2::frame_type::data)
            {
                if (!received_headers[index])
                    co_return results;
                auto& current = *results[index];
                current.set_body_preserve_headers(current.take_body() +
                    std::string(reinterpret_cast<const char*>(payload.data()), payload.size()));
            }
            else if (header->type == v2::frame_type::rst_stream)
            {
                results[index] = std::unexpected(make_error_code(std::errc::connection_aborted));
            }
            if ((header->flags & 0x1) != 0 && !completed[index])
            {
                completed[index] = true;
                --pending;
            }
        }
        else if (header->type == v2::frame_type::goaway)
        {
            for (std::size_t i = 0; i < results.size(); ++i)
                if (!completed[i])
                    results[i] = std::unexpected(make_error_code(std::errc::connection_aborted));
            co_return results;
        }
        input.erase(input.begin(), input.begin() + static_cast<std::ptrdiff_t>(required));
    }
    co_return results;
}

// =============================================================================
// Redirect Handling
// =============================================================================

#if defined(CNETMOD_HAS_SSL) && defined(CNETMOD_ENABLE_QUIC)
auto client::send_http3_tcp_race(const request& req, cancel_token& token)
    -> task<std::expected<response, std::error_code>>
{
    // Only replay-safe requests may be sent on both transports.  A streaming
    // body is one-shot and is intentionally excluded by the caller as well.
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));

    client_options h3_options = options_;
    h3_options.version_pref = http_version_preference::http3_only;
    h3_options.enable_alt_svc_http3 = false;
    h3_options.http3_fallback_to_tcp = false;
    client_options tcp_options = options_;
    tcp_options.version_pref = http_version_preference::http2_preferred;
    tcp_options.enable_alt_svc_http3 = false;
    tcp_options.http3_fallback_to_tcp = false;

    client h3_candidate{*ctx_, std::move(h3_options)};
    client tcp_candidate{*ctx_, std::move(tcp_options)};
    cancel_token h3_token;
    cancel_token tcp_token;

    struct race_outcome
    {
        int lane{};
        std::expected<response, std::error_code> result;
    };

    auto outcomes = std::make_shared<channel<race_outcome>>(2);

    auto run_candidate = [&, outcomes](int lane, client& candidate,
                             cancel_token& candidate_token) -> task<void>
    {
        auto result = co_await candidate.send(req, candidate_token);
        (void)co_await outcomes->send(
            race_outcome{lane, std::move(result)});
    };
    spawn(*ctx_, run_candidate(0, h3_candidate, h3_token));
    spawn(*ctx_, run_candidate(1, tcp_candidate, tcp_token));

    std::optional<race_outcome> winner;
    std::error_code first_error = make_error_code(std::errc::host_unreachable);
    std::size_t completed = 0;
    while (completed < 2 && !winner)
    {
        auto outcome = co_await outcomes->receive();
        if (!outcome)
            break;
        ++completed;
        if (outcome->result)
        {
            winner = std::move(*outcome);
            if (winner->lane == 0)
                tcp_token.cancel();
            else
                h3_token.cancel();
            break;
        }
        if (completed == 1)
            first_error = outcome->result.error();
    }

    // Keep both detached candidate coroutines alive until they have reported
    // completion.  This makes loser cancellation deterministic and keeps
    // their socket/TLS state valid while the cancellation propagates.
    while (completed < 2)
    {
        auto outcome = co_await outcomes->receive();
        if (!outcome)
            break;
        ++completed;
        if (!winner && outcome->result)
            winner = std::move(*outcome);
        else if (!winner && completed == 2)
            first_error = outcome->result.error();
    }

    if (winner)
    {
        raced_use_http3_ = winner->lane == 0;
        // Learn Alt-Svc from a successful raced response so subsequent
        // requests can use the already-proven QUIC endpoint directly.
        if (const auto parsed = url::parse(req.uri()); parsed &&
            parsed->scheme == "https" && options_.enable_alt_svc_http3)
        {
            const auto origin_port = parsed->port == 0U ? std::uint16_t{443} : parsed->port;
            remember_http3_alt_svc(parsed->host, origin_port,
                winner->result->get_header("Alt-Svc"));
        }
        co_return std::move(winner->result);
    }
    co_return std::unexpected(first_error);
}
#endif

auto client::send_with_redirects(const request& req, std::size_t redirect_count)
    -> task<std::expected<response, std::error_code>>
{
    cancel_token token;
    co_return co_await send_with_redirects(req, redirect_count, token);
}

auto client::send_with_redirects(const request& req, std::size_t redirect_count,
    cancel_token& token)
    -> task<std::expected<response, std::error_code>>
{
    if (redirect_count > options_.max_redirects)
    {
        co_return std::unexpected(make_error_code(std::errc::too_many_links));
    }

    // Parse URI
    auto uri = req.uri();
    std::string host;
    std::uint16_t port = 80;
    bool use_ssl = false;

    if (uri.starts_with("http://") || uri.starts_with("https://"))
    {
        auto url_result = url::parse(uri);
        if (!url_result)
        {
            co_return std::unexpected(make_error_code(http_errc::invalid_uri));
        }
        host = url_result->host;
        port = url_result->port;
        use_ssl = (url_result->scheme == "https");
    }
    else
    {
        if (!state_ || !state_->conn || !state_->conn->is_open())
        {
            co_return std::unexpected(make_error_code(std::errc::not_connected));
        }
        host = state_->host;
        port = state_->port;
        use_ssl = state_->is_ssl;
    }

#if defined(CNETMOD_HAS_SSL) && defined(CNETMOD_ENABLE_QUIC)
    const bool replay_safe = req.method() == http_method::GET ||
        req.method() == http_method::HEAD || req.method() == http_method::OPTIONS;
    if (redirect_count == 0 && options_.version_pref == http_version_preference::http3_preferred &&
        options_.enable_alt_svc_http3 && use_ssl && replay_safe &&
        !req.has_streaming_body() && !state_ && !h3_client_ &&
        !raced_use_http3_)
    {
        co_return co_await send_http3_tcp_race(req, token);
    }
#endif

    bool use_http3 = options_.version_pref == http_version_preference::http3_only ||
        options_.version_pref == http_version_preference::http3_preferred;
#if defined(CNETMOD_HAS_SSL) && defined(CNETMOD_ENABLE_QUIC)
    if (options_.version_pref == http_version_preference::http3_preferred &&
        raced_use_http3_)
        use_http3 = *raced_use_http3_;
#endif
    std::uint16_t http3_peer_port{};
#if defined(CNETMOD_HAS_SSL) && defined(CNETMOD_ENABLE_QUIC)
    if (options_.version_pref == http_version_preference::http2_preferred &&
        options_.enable_alt_svc_http3)
    {
        if (const auto alternative = http3_alt_svc_port(host, port))
        {
            use_http3 = true;
            http3_peer_port = *alternative;
        }
    }
#endif
    if (use_http3 && !use_ssl)
        co_return std::unexpected(make_error_code(std::errc::not_supported));

    // Connect if needed. A request with an absolute URL should still reuse the
    // existing keep-alive / HTTP/2 connection when it targets the same origin.
    if (!use_http3 && !host.empty())
    {
        const bool need_connect =
            !state_ ||
            !state_->conn ||
            !state_->conn->is_open() ||
            state_->host != host ||
            state_->port != port ||
            state_->is_ssl != use_ssl;
        if (need_connect)
        {
            auto connect_result = co_await connect(host, port, use_ssl, token);
            if (!connect_result)
            {
                co_return std::unexpected(connect_result.error());
            }
        }
    }

    // Dispatch based on protocol
    std::expected<response, std::error_code> result;

    if (use_http3)
    {
#if defined(CNETMOD_HAS_SSL) && defined(CNETMOD_ENABLE_QUIC)
        result = co_await send_http3(req, token, http3_peer_port);
        if (!result)
        {
            const bool replay_safe = req.method() == http_method::GET ||
                req.method() == http_method::HEAD || req.method() == http_method::OPTIONS;
            if (options_.version_pref == http_version_preference::http3_only ||
                !options_.http3_fallback_to_tcp || !replay_safe)
                co_return std::unexpected(result.error());
            use_http3 = false;
            const auto connected = co_await connect(host, port, use_ssl, token);
            if (!connected)
                co_return std::unexpected(connected.error());
        }
#else
        co_return std::unexpected(make_error_code(std::errc::not_supported));
#endif
    }

    if (!use_http3)
    {
        if (state_->protocol == protocol_type::http1)
        {
            result = co_await send_http1(req, token);
            if (!result && options_.http1_response_body_limit != 0)
                close();
        }
        else if (state_->protocol == protocol_type::http2)
        {
            result = co_await send_http2(req, token);
        }
        else
        {
            co_return std::unexpected(make_error_code(std::errc::not_supported));
        }
    }

    // Handle redirects
    if (result && options_.follow_redirects)
    {
        int status = result->status_code();

        if (status >= 300 && status < 400 && status != 304)
        {
            auto location = result->get_header("Location");

            if (!location.empty())
            {
                // A request body source is deliberately one-shot.  Following
                // a 307/308 (or replaying after a method-preserving redirect)
                // would silently send an empty or partial body.
                if (req.has_streaming_body())
                    co_return std::unexpected(
                        make_error_code(std::errc::operation_not_supported));
                request redirect_req = req;

                http_method new_method = req.method();

                switch (status)
                {
                case 301:
                case 302:
                case 303:
                    if (status == 303 &&
                        (req.method() == http_method::POST ||
                            req.method() == http_method::PUT))
                    {
                        new_method = http_method::GET;
                        redirect_req.set_method(new_method);
                        redirect_req.set_body(std::string{});
                    }
                    break;
                case 307:
                case 308:
                    break;
                default:
                    break;
                }

                std::string redirect_url;
                if (location.starts_with("http://") || location.starts_with("https://"))
                {
                    redirect_url = std::string(location);
                }
                else if (location.starts_with("/"))
                {
                    std::string scheme = use_ssl ? "https" : "http";
                    redirect_url = scheme + "://" + format_authority(host, port, use_ssl);
                    redirect_url += std::string(location);
                }
                else
                {
                    auto current_url = url::parse(uri);
                    if (current_url)
                    {
                        std::string scheme = current_url->scheme;
                        redirect_url = scheme + "://" +
                            format_authority_host(current_url->host);
                        if (current_url->port != 0 &&
                            ((scheme == "https" && current_url->port != 443) ||
                                (scheme == "http" && current_url->port != 80)))
                        {
                            redirect_url += ":" + std::to_string(current_url->port);
                        }

                        auto path = current_url->path;
                        auto last_slash = path.rfind('/');
                        if (last_slash != std::string::npos)
                        {
                            path = path.substr(0, last_slash + 1);
                        }
                        else
                        {
                            path = "/";
                        }
                        redirect_url += path + std::string(location);
                    }
                    else
                    {
                        std::string scheme = use_ssl ? "https" : "http";
                        redirect_url = scheme + "://" + host + "/" + std::string(location);
                    }
                }

                redirect_req.set_uri(redirect_url);

                co_return co_await send_with_redirects(redirect_req, redirect_count + 1, token);
            }
        }
    }

    // Alt-Svc is learned only from a successful HTTPS TCP response. A
    // subsequent request to this origin may then select HTTP/3.
#if defined(CNETMOD_HAS_SSL) && defined(CNETMOD_ENABLE_QUIC)
    if (result && !use_http3 && use_ssl && options_.enable_alt_svc_http3)
        remember_http3_alt_svc(host, port, result->get_header("Alt-Svc"));
#endif
    co_return result;
}

#if defined(CNETMOD_HAS_SSL) && defined(CNETMOD_ENABLE_QUIC)
auto client::send_http3(const request& req, cancel_token& token, std::uint16_t peer_port)
    -> task<std::expected<response, std::error_code>>
{
    if (token.is_cancelled())
        co_return std::unexpected(make_error_code(errc::operation_aborted));
    const auto parsed = url::parse(req.uri());
    if (!parsed || parsed->scheme != "https" || parsed->host.empty())
        co_return std::unexpected(make_error_code(http_errc::invalid_uri));
    if (!h3_ssl_ctx_)
        co_return std::unexpected(make_error_code(std::errc::not_supported));

    const auto port = parsed->port == 0U ? std::uint16_t{443} : parsed->port;
    const auto connect_port = peer_port == 0U ? port : peer_port;
    {
        co_await h3_lifecycle_mutex_.lock();
        cnetmod::async_lock_guard h3_guard{h3_lifecycle_mutex_, std::adopt_lock};
        const bool reusable = h3_client_ &&
            as_http3_client(h3_client_)->can_reuse_origin(parsed->host, port);

        if (!reusable)
        {
            v3::http3_client_options options;
            options.connect_timeout = options_.connect_timeout;
            options.request_timeout = options_.request_timeout;
            options.h3_qpack_max_table_capacity = options_.h3_qpack_max_table_capacity;
            options.h3_qpack_blocked_streams = options_.h3_qpack_blocked_streams;
            options.verify_certificate = options_.verify_peer;
            options.tls_sni_host = parsed->host;
            options.resumption_ticket = load_http3_resumption_ticket(
                options_.http3_resumption_ticket_file, parsed->host, port);
            options.enable_early_data = options_.enable_http3_early_data;
            options.max_push_id = options_.http3_max_push_id;
            options.on_server_push = options_.on_http3_push;
            h3_client_ = std::make_shared<v3::http3_client>(*ctx_, *h3_ssl_ctx_,
                std::move(options));
            const auto h3_client = as_http3_client(h3_client_);
            const auto connected = co_await h3_client->connect(parsed->host, connect_port,
                parsed->host, port);

            if (!connected)
                co_return std::unexpected(connected.error());
            if (const auto ticket = h3_client->take_resumption_ticket())
                persist_http3_resumption_ticket(options_.http3_resumption_ticket_file,
                    parsed->host, port, *ticket);
        }
    }

    // Keep the selected connection object alive for the complete request even
    // if another concurrent request replaces the client's pool entry.
    const auto h3_client = as_http3_client(h3_client_);
    if (!h3_client)
        co_return std::unexpected(std::make_error_code(std::errc::not_connected));

    v3::http3_request h3_request;
    h3_request.method = req.method();
    h3_request.scheme = parsed->scheme;
    h3_request.host = parsed->host;
    h3_request.port = port;
    h3_request.path = parsed->path.empty() ? "/" : parsed->path;
    if (!parsed->query.empty())
        h3_request.path += "?" + parsed->query;
    h3_request.headers = req.headers();
    h3_request.body = std::string(req.body());
    h3_request.body_source = req.body_source();

    const auto h3_response = co_await h3_client->send_request(h3_request, token);
    if (!h3_response)
        co_return std::unexpected(h3_response.error());

    response output{h3_response->status, http_version::http_3};
    for (const auto& [name, value] : h3_response->headers)
        output.append_header(name, value);
    for (const auto& [name, value] : h3_response->trailers)
        output.append_trailer(name, value);
    output.set_body(h3_response->body);
    // NewSessionTicket is post-handshake and may arrive while requests are
    // in flight. Refresh the optional cross-process cache after each
    // successful response; an unavailable ticket is intentionally ignored.
    if (const auto ticket = h3_client->take_resumption_ticket())
        persist_http3_resumption_ticket(options_.http3_resumption_ticket_file,
            parsed->host, port, *ticket);

    co_return output;
}

auto client::send_http3_batch_item(std::span<const request> requests,
    std::vector<std::expected<response, std::error_code>>& results,
    async_wait_group& completed, async_semaphore& permits, std::size_t index)
    -> task<void>
{
    co_await permits.acquire();

    cancel_token token;
    results[index] = co_await send_http3(requests[index], token);

    permits.release();
    completed.done();
}

auto client::send_http3_batch(std::span<const request> requests)
    -> task<std::vector<std::expected<response, std::error_code>>>
{
    std::vector<std::expected<response, std::error_code>> results;
    if (requests.empty())
        co_return results;

    const auto first = url::parse(requests.front().uri());
    if (!first || first->scheme != "https" || first->host.empty())
    {
        results.assign(requests.size(),
            std::unexpected(make_error_code(http_errc::invalid_uri)));
        co_return results;
    }
    const auto port = first->port == 0U ? std::uint16_t{443} : first->port;
    for (const auto& value : requests)
    {
        const auto parsed = url::parse(value.uri());
        if (!parsed || parsed->scheme != "https" || parsed->host != first->host ||
            (parsed->port == 0U ? std::uint16_t{443} : parsed->port) != port)
        {
            results.assign(requests.size(),
                std::unexpected(make_error_code(http_errc::invalid_uri)));
            co_return results;
        }
    }

    results.assign(requests.size(),
        std::unexpected(make_error_code(std::errc::operation_canceled)));
    // A cold batch still uses the first request to establish the QUIC/H3
    // session. Once an origin session is already ready, all batch items can be
    // submitted together; the session write gate is released after each FIN
    // and response reads proceed independently.
    std::size_t first_concurrent_index = 0U;
    const bool h3_ready = h3_client_ &&
        as_http3_client(h3_client_)->can_reuse_origin(first->host, port);
    if (!h3_ready)
    {
        cancel_token first_token;
        results[0] = co_await send_http3(requests.front(), first_token);

        first_concurrent_index = 1U;
    }
    async_wait_group completed;
    async_semaphore permits{std::max<std::size_t>(1, options_.h3_max_concurrent_streams)};
    for (std::size_t index = first_concurrent_index; index < requests.size(); ++index)
    {
        completed.add();
        spawn(*ctx_, send_http3_batch_item(requests, results, completed, permits, index));
    }
    co_await completed.wait();
    co_return results;
}

auto client::has_http3_alt_svc(std::string_view host, std::uint16_t port) const -> bool
{
    const auto key = std::string(host) + ":" + std::to_string(port);
    const auto found = h3_alt_svc_.find(key);
    return found != h3_alt_svc_.end() &&
        found->second.expires_at > std::chrono::steady_clock::now();
}

auto client::http3_alt_svc_port(std::string_view host, std::uint16_t port) const
    -> std::optional<std::uint16_t>
{
    const auto key = std::string(host) + ":" + std::to_string(port);
    const auto found = h3_alt_svc_.find(key);
    if (found == h3_alt_svc_.end() || found->second.expires_at <= std::chrono::steady_clock::now())
        return std::nullopt;
    return found->second.peer_port;
}

void client::remember_http3_alt_svc(std::string_view host, std::uint16_t port,
    std::string_view value)
{
    const auto h3 = value.find("h3=");
    if (h3 == std::string_view::npos)
        return;
    auto peer_port = port;
    auto advertised = value.substr(h3 + 3);
    if (!advertised.empty() && advertised.front() == '"')
    {
        advertised.remove_prefix(1);
        advertised = advertised.substr(0, advertised.find('"'));
    }
    else
    {
        advertised = advertised.substr(0, advertised.find(';'));
    }
    // RFC 7838 permits the compact form h3=":443". Host changes are not
    // accepted: the certificate/origin authorization model remains strict.
    if (advertised.starts_with(":"))
    {
        unsigned parsed_port{};
        const auto first = advertised.data() + 1;
        const auto last = advertised.data() + advertised.size();
        const auto [end, error] = std::from_chars(first, last, parsed_port);
        if (error != std::errc{} || end != last || parsed_port == 0U || parsed_port > 65535U)
            return;
        peer_port = static_cast<std::uint16_t>(parsed_port);
    }
    else if (!advertised.empty())
    {
        return;
    }
    std::chrono::seconds max_age{86400};
    if (const auto marker = value.find("ma="); marker != std::string_view::npos)
    {
        const auto digits = value.substr(marker + 3).substr(0, value.substr(marker + 3).find_first_not_of("0123456789"));
        unsigned long long seconds{};
        const auto [end, error] = std::from_chars(digits.data(), digits.data() + digits.size(), seconds);
        if (!digits.empty() && error == std::errc{} && end == digits.data() + digits.size())
            max_age = std::chrono::seconds{std::min<unsigned long long>(seconds, 7U * 24U * 60U * 60U)};
    }
    const auto key = std::string(host) + ":" + std::to_string(port);
    if (max_age == std::chrono::seconds::zero())
    {
        h3_alt_svc_.erase(key);
        persist_alt_svc_cache();
        return;
    }
    h3_alt_svc_[key] = {std::chrono::steady_clock::now() + max_age, peer_port};
    persist_alt_svc_cache();
}
#endif

// =============================================================================
// Main Send Methods
// =============================================================================

auto client::send(const request& req)
    -> task<std::expected<response, std::error_code>>
{
    cancel_token token;
    co_return co_await send(req, token);
}

auto client::send(const request& req, cancel_token& token)
    -> task<std::expected<response, std::error_code>>
{
    co_return co_await send_with_redirects(req, 0, token);
}

auto client::send(const request& req, deadline request_deadline)
    -> task<std::expected<response, std::error_code>>
{
    co_return co_await with_deadline(*ctx_, request_deadline,
        [&](cancel_token& token)
        {
            return send(req, token);
        });
}

auto client::send(http_method method, std::string_view url, std::string_view body)
    -> task<std::expected<response, std::error_code>>
{
    request req(method, url);

    if (!body.empty())
    {
        req.set_body(std::string(body));
        if (req.get_header("Content-Type").empty())
        {
            req.set_header("Content-Type", "application/octet-stream");
        }
    }

    co_return co_await send(req);
}

auto client::send_batch(std::span<const request> requests)
    -> task<std::vector<std::expected<response, std::error_code>>>
{
    std::vector<std::expected<response, std::error_code>> results;
    if (requests.empty())
        co_return results;

    if (std::ranges::any_of(requests,
            [](const request& req)
            {
                return req.has_streaming_body();
            }))
    {
        results.assign(requests.size(),
            std::unexpected(make_error_code(std::errc::operation_not_supported)));
        co_return results;
    }

    if (options_.version_pref == http_version_preference::http3_only ||
        options_.version_pref == http_version_preference::http3_preferred)
    {
#if defined(CNETMOD_HAS_SSL) && defined(CNETMOD_ENABLE_QUIC)
        co_return co_await send_http3_batch(requests);
#else
        results.assign(requests.size(),
            std::unexpected(make_error_code(std::errc::not_supported)));
        co_return results;
#endif
    }

    const auto first_uri = requests.front().uri();
    std::string host;
    std::uint16_t port{};
    bool use_ssl{};
    if (first_uri.starts_with("http://") || first_uri.starts_with("https://"))
    {
        const auto parsed = url::parse(first_uri);
        if (!parsed)
        {
            for (std::size_t i = 0; i < requests.size(); ++i)
                results.emplace_back(std::unexpected(make_error_code(http_errc::invalid_uri)));
            co_return results;
        }
        host = parsed->host;
        port = parsed->port;
        use_ssl = parsed->scheme == "https";
#if defined(CNETMOD_HAS_SSL) && defined(CNETMOD_ENABLE_QUIC)
        if (use_ssl && options_.version_pref == http_version_preference::http2_preferred &&
            options_.enable_alt_svc_http3 && has_http3_alt_svc(host, port))
        {
            co_return co_await send_http3_batch(requests);
        }
#endif
        const auto connected = co_await connect(host, port, use_ssl);
        if (!connected)
        {
            for (std::size_t i = 0; i < requests.size(); ++i)
                results.emplace_back(std::unexpected(connected.error()));
            co_return results;
        }
    }
    else if (state_ && state_->conn && state_->conn->is_open())
    {
        host = state_->host;
        port = state_->port;
        use_ssl = state_->is_ssl;
    }
    else
    {
        for (std::size_t i = 0; i < requests.size(); ++i)
            results.emplace_back(std::unexpected(make_error_code(std::errc::not_connected)));
        co_return results;
    }

    for (const auto& request : requests)
    {
        const auto uri = request.uri();
        if (!uri.starts_with("http://") && !uri.starts_with("https://"))
            continue;
        const auto parsed = url::parse(uri);
        if (!parsed || parsed->host != host || parsed->port != port ||
            (parsed->scheme == "https") != use_ssl)
        {
            for (std::size_t i = 0; i < requests.size(); ++i)
                results.emplace_back(std::unexpected(make_error_code(http_errc::invalid_uri)));
            co_return results;
        }
    }

    if (state_->protocol == protocol_type::http2)
    {
        co_return co_await send_http2_batch(requests);
    }
    results.reserve(requests.size());
    for (const auto& request : requests)
    {
        results.emplace_back(co_await send_http1(request));
    }
    co_return results;
}

} // namespace cnetmod::http
