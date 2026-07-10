module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.http;

import std;
import :semantics;
import :request;
import :response;
import :parser;
import :cookie;
import :client;
import cnetmod.core.error;
import cnetmod.core.buffer;
import cnetmod.core.socket;
import cnetmod.core.address;
import cnetmod.core.dns;
import cnetmod.io.io_context;
import cnetmod.protocol.tcp;
import cnetmod.coro.task;
import cnetmod.executor.async_op;
import cnetmod.protocol.http.v2.frame;
import cnetmod.protocol.http.v2.settings;
import cnetmod.protocol.http.v2.header_compression;

#ifdef CNETMOD_HAS_SSL
import cnetmod.core.ssl;
#endif

namespace cnetmod::http {

// =============================================================================
// SSL Context Initialization
// =============================================================================

#ifdef CNETMOD_HAS_SSL
void client::init_ssl_context() {
    auto ctx_result = ssl_context::client();
    if (!ctx_result) {
        return;
    }
    
    ssl_ctx_ = std::move(*ctx_result);
    
    if (!options_.ca_file.empty()) {
        (void)ssl_ctx_->load_ca_file(options_.ca_file);
    } else {
        (void)ssl_ctx_->set_default_ca();
    }
    
    if (!options_.cert_file.empty()) {
        (void)ssl_ctx_->load_cert_file(options_.cert_file);
    }
    if (!options_.key_file.empty()) {
        (void)ssl_ctx_->load_key_file(options_.key_file);
    }
    
    ssl_ctx_->set_verify_peer(options_.verify_peer);
    
    // Configure ALPN based on version preference
    switch (options_.version_pref) {
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
    }
}
#endif

// =============================================================================
// Connection Management
// =============================================================================

void client::close() noexcept {
    if (!state_) return;
    
#ifdef CNETMOD_HAS_SSL
    if (state_->ssl) {
        state_->ssl.reset();
    }
#endif
    
    if (state_->conn) {
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
    // Reuse connection if same host:port:ssl
    if (state_ && state_->conn && state_->conn->is_open() &&
        state_->host == host && state_->port == port && state_->is_ssl == use_ssl) {
        co_return {};
    }

    close();

    state_.emplace();
    state_->host = std::string(host);
    state_->port = port;
    state_->is_ssl = use_ssl;

    auto connect_r = co_await async_connect_happy_eyeballs(*ctx_, host, port);
    if (!connect_r) {
        co_return std::unexpected(connect_r.error());
    }

    auto sock = std::move(connect_r->sock);

    state_->conn.emplace(*ctx_, std::move(sock));

#ifdef CNETMOD_HAS_SSL
    if (use_ssl) {
        if (!ssl_ctx_) {
            co_return std::unexpected(make_error_code(std::errc::not_supported));
        }
        
        // Create SSL stream
        state_->ssl.emplace(*ssl_ctx_, *ctx_, state_->conn->native_socket());
        state_->ssl->set_hostname(host);
        state_->ssl->set_connect_state();
        
        // Async SSL handshake
        auto handshake_result = co_await state_->ssl->async_handshake();
        if (!handshake_result) {
            close();
            co_return std::unexpected(handshake_result.error());
        }
        
        // Check ALPN negotiation
        auto alpn = state_->ssl->get_alpn_selected();
        if (alpn == "h2") {
            state_->protocol = protocol_type::http2;
        } else {
            state_->protocol = protocol_type::http1;
        }
    } else
#endif
    {
        // Plain HTTP - check version preference
        switch (options_.version_pref) {
        case http_version_preference::http2_only: {
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
    if (!state_ || !state_->conn || !state_->conn->is_open()) {
        co_return std::unexpected(make_error_code(std::errc::not_connected));
    }

#ifdef CNETMOD_HAS_SSL
    if (state_->is_ssl && state_->ssl) {
        co_return co_await state_->ssl->async_write_all(
            const_buffer{data.data(), data.size()});
    }
#endif

    auto& sock = state_->conn->native_socket();
    co_return co_await async_write_all(*ctx_, sock,
        const_buffer{data.data(), data.size()});
}

auto client::read_data(void* buffer, std::size_t size)
    -> task<std::expected<std::size_t, std::error_code>>
{
    if (!state_ || !state_->conn || !state_->conn->is_open()) {
        co_return std::unexpected(make_error_code(std::errc::not_connected));
    }

#ifdef CNETMOD_HAS_SSL
    if (state_->is_ssl && state_->ssl) {
        co_return co_await state_->ssl->async_read(mutable_buffer{buffer, size});
    }
#endif

    auto& sock = state_->conn->native_socket();
    co_return co_await async_read(*ctx_, sock, mutable_buffer{buffer, size});
}

// =============================================================================
// HTTP/1.1 Implementation
// =============================================================================

auto client::send_http1(const request& req)
    -> task<std::expected<response, std::error_code>>
{
    if (!state_) {
        co_return std::unexpected(make_error_code(std::errc::not_connected));
    }

    // Extract path from URI
    auto uri = req.uri();
    std::string path = "/";
    
    if (uri.starts_with("http://") || uri.starts_with("https://")) {
        auto url_result = url::parse(uri);
        if (!url_result) {
            co_return std::unexpected(make_error_code(http_errc::invalid_uri));
        }
        path = url_result->path;
        if (!url_result->query.empty()) {
            path += "?";
            path += url_result->query;
        }
    } else {
        path = std::string(uri);
    }

    std::string cookie_header;
    if (options_.enable_cookies && req.get_header("Cookie").empty()) {
        auto generated_cookie = cookies_.to_cookie_header(
            state_->host, path, state_->is_ssl);
        if (!generated_cookie.empty()) cookie_header = std::move(generated_cookie);
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

    for (const auto& [key, value] : req.headers()) {
        request_data += key;
        request_data += ": ";
        request_data += value;
        request_data += "\r\n";
    }

    if (req.get_header("Host").empty()) {
        request_data += "Host: ";
        request_data += format_authority(state_->host, state_->port, state_->is_ssl);
        request_data += "\r\n";
    }

    if (req.get_header("User-Agent").empty()) {
        request_data += "User-Agent: ";
        request_data += options_.user_agent;
        request_data += "\r\n";
    }

    if (req.get_header("Connection").empty()) {
        request_data += "Connection: ";
        request_data += options_.keep_alive ? "keep-alive" : "close";
        request_data += "\r\n";
    }

    if (!cookie_header.empty()) {
        request_data += "Cookie: ";
        request_data += cookie_header;
        request_data += "\r\n";
    }

    request_data += "\r\n";
    if (!req.body().empty()) {
        request_data += req.body();
    }

    // Send request
    auto send_result = co_await write_data(request_data);
    if (!send_result) {
        co_return std::unexpected(send_result.error());
    }

    // Receive response - read until we have complete headers
    auto& buffer = state_->read_buffer;
    buffer.clear();
    buffer.reserve(4096);
    std::size_t header_end = std::string::npos;
    
    while (header_end == std::string::npos) {
        char temp[4096];
        auto result = co_await read_data(temp, sizeof(temp));
        if (!result) {
            co_return std::unexpected(result.error());
        }
        
        if (*result == 0) {
            co_return std::unexpected(make_error_code(std::errc::connection_reset));
        }

        buffer.append(temp, *result);
        header_end = buffer.find("\r\n\r\n");

        if (buffer.size() > max_header_size) {
            co_return std::unexpected(make_error_code(http_errc::header_too_large));
        }
    }

    // Parse status line and headers
    response resp;
    std::string_view view = buffer;
    
    // Parse status line
    auto line_end = view.find("\r\n");
    if (line_end == std::string_view::npos) {
        co_return std::unexpected(make_error_code(http_errc::invalid_status_line));
    }

    auto status_line = view.substr(0, line_end);
    view = view.substr(line_end + 2);

    // Parse "HTTP/1.1 200 OK"
    auto space1 = status_line.find(' ');
    if (space1 == std::string_view::npos) {
        co_return std::unexpected(make_error_code(http_errc::invalid_status_line));
    }

    auto version_str = status_line.substr(0, space1);
    auto version_opt = string_to_version(version_str);
    if (!version_opt) {
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
    if (ec != std::errc{}) {
        co_return std::unexpected(make_error_code(http_errc::invalid_status_line));
    }
    resp.set_status(status_code);

    if (space2 != std::string_view::npos) {
        resp.set_status_message(rest.substr(space2 + 1));
    }

    // Parse headers
    while (true) {
        line_end = view.find("\r\n");
        if (line_end == std::string_view::npos) {
            break;
        }

        auto line = view.substr(0, line_end);
        if (line.empty()) {
            view = view.substr(2);
            break;
        }

        auto colon = line.find(':');
        if (colon == std::string_view::npos) {
            co_return std::unexpected(make_error_code(http_errc::invalid_header));
        }

        auto key = line.substr(0, colon);
        auto value = line.substr(colon + 1);
        
        // Trim whitespace
        while (!value.empty() && value[0] == ' ') value = value.substr(1);
        while (!value.empty() && value.back() == ' ') 
            value = value.substr(0, value.size() - 1);

        resp.set_header(key, value);
        view = view.substr(line_end + 2);
    }

    // Read body
    auto& body = state_->body_buffer;
    body.clear();
    auto content_length_str = resp.get_header("Content-Length");
    
    if (!content_length_str.empty()) {
        std::size_t content_length = 0;
        auto [ptr2, ec2] = std::from_chars(content_length_str.data(),
                                          content_length_str.data() + 
                                          content_length_str.size(),
                                          content_length);
        if (ec2 == std::errc{}) {
            if (content_length > max_body_size) {
                co_return std::unexpected(make_error_code(http_errc::body_too_large));
            }

            body.reserve(content_length);
            
            // Append any body data already in buffer
            if (!view.empty()) {
                body.append(view);
            }

            // Read remaining body
            while (body.size() < content_length) {
                char temp[4096];
                auto to_read = std::min(sizeof(temp), content_length - body.size());
                auto result = co_await read_data(temp, to_read);
                if (!result) {
                    co_return std::unexpected(result.error());
                }
                if (*result == 0) {
                    break;
                }
                body.append(temp, *result);
            }
        }
    } else if (resp.get_header("Transfer-Encoding").find("chunked") != 
               std::string_view::npos) {
        // Handle chunked encoding - complete
        std::string remaining_data(view);
        
        while (true) {
            // Chunk size
            auto crlf_pos = remaining_data.find("\r\n");
            
            // Complete, read
            while (crlf_pos == std::string::npos) {
                char temp[4096];
                auto result = co_await read_data(temp, sizeof(temp));
                if (!result) {
                    co_return std::unexpected(result.error());
                }
                if (*result == 0) {
                    co_return std::unexpected(make_error_code(std::errc::connection_reset));
                }
                remaining_data.append(temp, *result);
                crlf_pos = remaining_data.find("\r\n");
            }
            
            // Parse chunk size ()
            auto size_str = remaining_data.substr(0, crlf_pos);
            
            // Implementation note.
            auto semicolon = size_str.find(';');
            if (semicolon != std::string::npos) {
                size_str = size_str.substr(0, semicolon);
            }
            
            std::size_t chunk_size = 0;
            auto [ptr, ec] = std::from_chars(
                size_str.data(), 
                size_str.data() + size_str.size(), 
                chunk_size, 16);  // Implementation note.
            
            if (ec != std::errc{}) {
                co_return std::unexpected(make_error_code(http_errc::invalid_chunk));
            }
            
            // Chunk size 0
            if (chunk_size == 0) {
                // Read trailer headers()
                remaining_data = remaining_data.substr(crlf_pos + 2);
                
                // Implementation note: trailer.
                while (true) {
                    auto trailer_crlf = remaining_data.find("\r\n");
                    if (trailer_crlf == std::string::npos) {
                        char temp[4096];
                        auto result = co_await read_data(temp, sizeof(temp));
                        if (!result || *result == 0) break;
                        remaining_data.append(temp, *result);
                        continue;
                    }
                    
                    if (trailer_crlf == 0) {
                        // Implementation note.
                        break;
                    }
                    
                    // Implementation note.
                    remaining_data = remaining_data.substr(trailer_crlf + 2);
                }
                
                break;  // Chunks read
            }
            
            // Implementation note: size.
            remaining_data = remaining_data.substr(crlf_pos + 2);
            
            // (chunk data + \r\n)
            while (remaining_data.size() < chunk_size + 2) {
                char temp[4096];
                auto result = co_await read_data(temp, sizeof(temp));
                if (!result) {
                    co_return std::unexpected(result.error());
                }
                if (*result == 0) {
                    co_return std::unexpected(make_error_code(std::errc::connection_reset));
                }
                remaining_data.append(temp, *result);
            }
            
            // Implementation note: chunk.
            body.append(remaining_data.substr(0, chunk_size));
            
            // Chunk \r\n
            remaining_data = remaining_data.substr(chunk_size + 2);
        }
    }

    resp.set_body_preserve_headers(std::move(body));

    // Set-Cookie
    if (options_.enable_cookies) {
        for (const auto& [key, value] : resp.headers()) {
            if (key == "Set-Cookie" || key == "set-cookie") {
                cookies_.add_from_header(value);
            }
        }
    }

    // Handle Connection header
    auto connection = resp.get_header("Connection");
    if (!options_.keep_alive || connection == "close") {
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
    if (!state_ || !state_->conn) {
        co_return std::unexpected(make_error_code(std::errc::not_connected));
    }
    auto append_frame = [](std::vector<std::byte>& output, v2::frame_header header,
                           std::span<const std::byte> payload) {
        header.length = static_cast<std::uint32_t>(payload.size());
        const auto encoded = v2::encode_frame_header(header);
        output.insert(output.end(), encoded.begin(), encoded.end());
        output.insert(output.end(), payload.begin(), payload.end());
    };
    std::vector<std::byte> outbound;
    if (!state_->h2_initialized) {
        constexpr std::string_view preface = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
        for (const auto ch : preface) outbound.push_back(static_cast<std::byte>(ch));
        const v2::settings settings{.max_concurrent_streams = options_.h2_max_concurrent_streams,
                                    .initial_window_size = options_.h2_initial_window_size};
        const auto values = v2::encode_settings(settings);
        append_frame(outbound, {.type = v2::frame_type::settings}, values);
        state_->h2_initialized = true;
    }

    const auto stream_id = state_->h2_next_stream_id;
    if (stream_id == 0 || stream_id > 0x7fff'fffdU) {
        co_return std::unexpected(make_error_code(std::errc::resource_unavailable_try_again));
    }
    state_->h2_next_stream_id += 2;
    std::string authority = std::string(req.get_header("Host"));
    if (authority.empty()) authority = format_authority(state_->host, state_->port, state_->is_ssl);
    std::string target(req.uri());
    if (target.starts_with("http://") || target.starts_with("https://")) {
        auto parsed = url::parse(target);
        if (!parsed) co_return std::unexpected(make_error_code(http_errc::invalid_uri));
        target = parsed->path;
        if (!parsed->query.empty()) {
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
    for (const auto& [name, value] : req.headers()) {
        std::string lowercase;
        lowercase.reserve(name.size());
        for (const auto ch : name) lowercase.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
        if (lowercase == "host" || lowercase == "connection" || lowercase == "transfer-encoding" ||
            lowercase == "upgrade" || lowercase == "keep-alive") continue;
        fields.push_back({std::move(lowercase), value,
                          lowercase == "authorization" || lowercase == "cookie"});
    }
    const auto block = state_->h2_encoder.encode(fields);
    if (!block || block->size() > 16 * 1024) {
        co_return std::unexpected(block ? make_error_code(std::errc::message_size) : block.error());
    }
    const bool has_body = !req.body().empty();
    append_frame(outbound, {.type = v2::frame_type::headers,
                            .flags = static_cast<std::uint8_t>(0x4 | (has_body ? 0 : 0x1)),
                            .stream_id = stream_id}, *block);
    if (has_body) {
        const auto body = req.body();
        append_frame(outbound, {.type = v2::frame_type::data, .flags = 0x1, .stream_id = stream_id},
                     {reinterpret_cast<const std::byte*>(body.data()), body.size()});
    }
    const auto write = co_await write_data({reinterpret_cast<const char*>(outbound.data()), outbound.size()});
    if (!write) co_return std::unexpected(write.error());

    response result(200, http_version::http_2);
    bool received_headers = false;
    bool completed = false;
    std::vector<std::byte> input;
    input.reserve(16 * 1024);
    std::array<std::byte, 16 * 1024> buffer{};
    while (!completed) {
        while (input.size() < v2::frame_header_size) {
            const auto read = co_await read_data(buffer.data(), buffer.size());
            if (!read || *read == 0) co_return std::unexpected(read ? make_error_code(std::errc::connection_reset) : read.error());
            input.insert(input.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(*read));
        }
        const auto header = v2::decode_frame_header({input.data(), v2::frame_header_size});
        if (!header || header->length > 16 * 1024 * 1024) co_return std::unexpected(header ? make_error_code(std::errc::message_size) : header.error());
        const auto required = v2::frame_header_size + static_cast<std::size_t>(header->length);
        while (input.size() < required) {
            const auto read = co_await read_data(buffer.data(), buffer.size());
            if (!read || *read == 0) co_return std::unexpected(read ? make_error_code(std::errc::connection_reset) : read.error());
            input.insert(input.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(*read));
        }
        const auto payload = std::span{input.data() + v2::frame_header_size, static_cast<std::size_t>(header->length)};
        if (header->type == v2::frame_type::settings && (header->flags & 0x1) == 0) {
            v2::settings peer;
            if (const auto settings_error = v2::decode_settings(payload, peer); settings_error)
                co_return std::unexpected(settings_error);
            state_->h2_encoder.set_dynamic_table_limit(peer.header_table_size);
            const std::array<std::byte, 0> empty{};
            std::vector<std::byte> ack;
            append_frame(ack, {.type = v2::frame_type::settings, .flags = 0x1}, empty);
            const auto ack_write = co_await write_data({reinterpret_cast<const char*>(ack.data()), ack.size()});
            if (!ack_write) co_return std::unexpected(ack_write.error());
        } else if (header->type == v2::frame_type::headers && header->stream_id == stream_id) {
            const auto decoded = state_->h2_decoder.decode(payload);
            if (!decoded) co_return std::unexpected(decoded.error());
            for (const auto& field : *decoded) {
                if (field.name == ":status") {
                    int status{};
                    const auto [end, ec] = std::from_chars(field.value.data(), field.value.data() + field.value.size(), status);
                    if (ec != std::errc{} || end != field.value.data() + field.value.size())
                        co_return std::unexpected(make_error_code(std::errc::protocol_error));
                    result.set_status(status);
                } else if (!field.name.starts_with(':')) {
                    result.append_header(field.name, field.value);
                }
            }
            received_headers = true;
            completed = (header->flags & 0x1) != 0;
        } else if (header->type == v2::frame_type::data && header->stream_id == stream_id) {
            if (!received_headers) co_return std::unexpected(make_error_code(std::errc::protocol_error));
            result.set_body_preserve_headers(result.take_body() + std::string(reinterpret_cast<const char*>(payload.data()), payload.size()));
            completed = (header->flags & 0x1) != 0;
        } else if (header->type == v2::frame_type::rst_stream && header->stream_id == stream_id) {
            co_return std::unexpected(make_error_code(std::errc::connection_aborted));
        } else if (header->type == v2::frame_type::goaway) {
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
    for (std::size_t i = 0; i < requests.size(); ++i) {
        results.emplace_back(std::unexpected(make_error_code(std::errc::operation_canceled)));
    }
    if (requests.empty()) co_return results;
    if (!state_ || !state_->conn) co_return results;

    auto append_frame = [](std::vector<std::byte>& output, v2::frame_header header,
                           std::span<const std::byte> payload) {
        header.length = static_cast<std::uint32_t>(payload.size());
        const auto encoded = v2::encode_frame_header(header);
        output.insert(output.end(), encoded.begin(), encoded.end());
        output.insert(output.end(), payload.begin(), payload.end());
    };
    auto request_target = [](const request& request) -> std::expected<std::string, std::error_code> {
        std::string target(request.uri());
        if (!target.starts_with("http://") && !target.starts_with("https://")) return target;
        const auto parsed = url::parse(target);
        if (!parsed) return std::unexpected(make_error_code(http_errc::invalid_uri));
        target = parsed->path;
        if (!parsed->query.empty()) {
            target.push_back('?');
            target += parsed->query;
        }
        return target;
    };

    std::vector<std::byte> outbound;
    if (!state_->h2_initialized) {
        constexpr std::string_view preface = "PRI * HTTP/2.0\r\n\r\nSM\r\n\r\n";
        outbound.reserve(preface.size() + requests.size() * 128);
        for (const auto ch : preface) outbound.push_back(static_cast<std::byte>(ch));
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

    for (std::size_t index = 0; index < requests.size(); ++index) {
        const auto& req = requests[index];
        const auto stream_id = state_->h2_next_stream_id;
        if (stream_id == 0 || stream_id > 0x7fff'fffdU) co_return results;
        state_->h2_next_stream_id += 2;
        auto target = request_target(req);
        if (!target) co_return results;
        std::string authority(req.get_header("Host"));
        if (authority.empty()) authority = format_authority(state_->host, state_->port, state_->is_ssl);
        std::vector<v2::header_field> fields{
            {":method", std::string(method_to_string(req.method()))},
            {":scheme", state_->is_ssl ? "https" : "http"},
            {":authority", std::move(authority)},
            {":path", std::move(*target)},
        };
        fields.reserve(fields.size() + req.headers().size());
        for (const auto& [name, value] : req.headers()) {
            std::string lowercase;
            lowercase.reserve(name.size());
            for (const auto ch : name) {
                lowercase.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
            }
            if (lowercase == "host" || lowercase == "connection" || lowercase == "transfer-encoding" ||
                lowercase == "upgrade" || lowercase == "keep-alive") continue;
            const bool sensitive = lowercase == "authorization" || lowercase == "cookie";
            fields.push_back({std::move(lowercase), value, sensitive});
        }
        const auto block = state_->h2_encoder.encode(fields);
        if (!block || block->size() > 16 * 1024) co_return results;
        const bool has_body = !req.body().empty();
        append_frame(outbound, {.type = v2::frame_type::headers,
                                .flags = static_cast<std::uint8_t>(0x4 | (has_body ? 0 : 0x1)),
                                .stream_id = stream_id}, *block);
        const auto body = req.body();
        for (std::size_t offset = 0; offset < body.size();) {
            const auto size = std::min<std::size_t>(16 * 1024, body.size() - offset);
            const bool final = offset + size == body.size();
            append_frame(outbound, {.type = v2::frame_type::data,
                                    .flags = static_cast<std::uint8_t>(final ? 0x1 : 0),
                                    .stream_id = stream_id},
                         {reinterpret_cast<const std::byte*>(body.data() + offset), size});
            offset += size;
        }
        stream_indexes.emplace(stream_id, index);
    }

    const auto write = co_await write_data({reinterpret_cast<const char*>(outbound.data()), outbound.size()});
    if (!write) {
        for (auto& result : results) result = std::unexpected(write.error());
        co_return results;
    }

    std::size_t pending = requests.size();
    std::vector<std::byte> input;
    input.reserve(16 * 1024);
    std::array<std::byte, 16 * 1024> buffer{};
    while (pending != 0) {
        while (input.size() < v2::frame_header_size) {
            const auto read = co_await read_data(buffer.data(), buffer.size());
            if (!read || *read == 0) {
                const auto error = read ? make_error_code(std::errc::connection_reset) : read.error();
                for (std::size_t i = 0; i < results.size(); ++i)
                    if (!completed[i]) results[i] = std::unexpected(error);
                co_return results;
            }
            input.insert(input.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(*read));
        }
        const auto header = v2::decode_frame_header({input.data(), v2::frame_header_size});
        if (!header || header->length > 16 * 1024 * 1024) co_return results;
        const auto required = v2::frame_header_size + static_cast<std::size_t>(header->length);
        while (input.size() < required) {
            const auto read = co_await read_data(buffer.data(), buffer.size());
            if (!read || *read == 0) co_return results;
            input.insert(input.end(), buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(*read));
        }
        const auto payload = std::span{input.data() + v2::frame_header_size,
                                       static_cast<std::size_t>(header->length)};
        if (header->type == v2::frame_type::settings && (header->flags & 0x1) == 0) {
            v2::settings peer;
            if (const auto error = v2::decode_settings(payload, peer); error) co_return results;
            state_->h2_encoder.set_dynamic_table_limit(peer.header_table_size);
            std::vector<std::byte> ack;
            append_frame(ack, {.type = v2::frame_type::settings, .flags = 0x1}, {});
            const auto ack_write = co_await write_data({reinterpret_cast<const char*>(ack.data()), ack.size()});
            if (!ack_write) co_return results;
        } else if (header->type == v2::frame_type::ping && (header->flags & 0x1) == 0) {
            std::vector<std::byte> ack;
            append_frame(ack, {.type = v2::frame_type::ping, .flags = 0x1}, payload);
            const auto ack_write = co_await write_data({reinterpret_cast<const char*>(ack.data()), ack.size()});
            if (!ack_write) co_return results;
        } else if (const auto found = stream_indexes.find(header->stream_id); found != stream_indexes.end()) {
            const auto index = found->second;
            if (header->type == v2::frame_type::headers) {
                const auto decoded = state_->h2_decoder.decode(payload);
                if (!decoded) co_return results;
                response current(200, http_version::http_2);
                if (received_headers[index]) current = std::move(*results[index]);
                for (const auto& field : *decoded) {
                    if (field.name == ":status") {
                        int status{};
                        const auto [end, error] = std::from_chars(field.value.data(), field.value.data() + field.value.size(), status);
                        if (error != std::errc{} || end != field.value.data() + field.value.size()) co_return results;
                        current.set_status(status);
                    } else if (!field.name.starts_with(':')) {
                        current.append_header(field.name, field.value);
                    }
                }
                results[index] = std::move(current);
                received_headers[index] = true;
            } else if (header->type == v2::frame_type::data) {
                if (!received_headers[index]) co_return results;
                auto& current = *results[index];
                current.set_body_preserve_headers(current.take_body() +
                    std::string(reinterpret_cast<const char*>(payload.data()), payload.size()));
            } else if (header->type == v2::frame_type::rst_stream) {
                results[index] = std::unexpected(make_error_code(std::errc::connection_aborted));
            }
            if ((header->flags & 0x1) != 0 && !completed[index]) {
                completed[index] = true;
                --pending;
            }
        } else if (header->type == v2::frame_type::goaway) {
            for (std::size_t i = 0; i < results.size(); ++i)
                if (!completed[i]) results[i] = std::unexpected(make_error_code(std::errc::connection_aborted));
            co_return results;
        }
        input.erase(input.begin(), input.begin() + static_cast<std::ptrdiff_t>(required));
    }
    co_return results;
}

// =============================================================================
// Redirect Handling
// =============================================================================

auto client::send_with_redirects(const request& req, std::size_t redirect_count)
    -> task<std::expected<response, std::error_code>>
{
    if (redirect_count > options_.max_redirects) {
        co_return std::unexpected(make_error_code(std::errc::too_many_links));
    }

    // Parse URI
    auto uri = req.uri();
    std::string host;
    std::uint16_t port = 80;
    bool use_ssl = false;

    if (uri.starts_with("http://") || uri.starts_with("https://")) {
        auto url_result = url::parse(uri);
        if (!url_result) {
            co_return std::unexpected(make_error_code(http_errc::invalid_uri));
        }
        host = url_result->host;
        port = url_result->port;
        use_ssl = (url_result->scheme == "https");
    } else {
        if (!state_ || !state_->conn || !state_->conn->is_open()) {
            co_return std::unexpected(make_error_code(std::errc::not_connected));
        }
        host = state_->host;
        port = state_->port;
        use_ssl = state_->is_ssl;
    }

    // Connect if needed. A request with an absolute URL should still reuse the
    // existing keep-alive / HTTP/2 connection when it targets the same origin.
    if (!host.empty()) {
        const bool need_connect =
            !state_ ||
            !state_->conn ||
            !state_->conn->is_open() ||
            state_->host != host ||
            state_->port != port ||
            state_->is_ssl != use_ssl;
        if (need_connect) {
            auto connect_result = co_await connect(host, port, use_ssl);
            if (!connect_result) {
                co_return std::unexpected(connect_result.error());
            }
        }
    }

    // Dispatch based on protocol
    std::expected<response, std::error_code> result;
    
    if (state_->protocol == protocol_type::http1) {
        result = co_await send_http1(req);
    }
    else if (state_->protocol == protocol_type::http2) {
        result = co_await send_http2(req);
    }
    else {
        co_return std::unexpected(make_error_code(std::errc::not_supported));
    }

    // Handle redirects
    if (result && options_.follow_redirects) {
        int status = result->status_code();
        
        if (status >= 300 && status < 400 && status != 304) {
            auto location = result->get_header("Location");
            
            if (!location.empty()) {
                request redirect_req = req;
                
                http_method new_method = req.method();
                
                switch (status) {
                case 301:
                case 302:
                case 303:
                    if (status == 303 && 
                        (req.method() == http_method::POST || 
                         req.method() == http_method::PUT)) {
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
                if (location.starts_with("http://") || location.starts_with("https://")) {
                    redirect_url = std::string(location);
                } else if (location.starts_with("/")) {
                    std::string scheme = use_ssl ? "https" : "http";
                    redirect_url = scheme + "://" + format_authority(host, port, use_ssl);
                    redirect_url += std::string(location);
                } else {
                    auto current_url = url::parse(uri);
                    if (current_url) {
                        std::string scheme = current_url->scheme;
                        redirect_url = scheme + "://" +
                            format_authority_host(current_url->host);
                        if (current_url->port != 0 && 
                            ((scheme == "https" && current_url->port != 443) ||
                             (scheme == "http" && current_url->port != 80))) {
                            redirect_url += ":" + std::to_string(current_url->port);
                        }
                        
                        auto path = current_url->path;
                        auto last_slash = path.rfind('/');
                        if (last_slash != std::string::npos) {
                            path = path.substr(0, last_slash + 1);
                        } else {
                            path = "/";
                        }
                        redirect_url += path + std::string(location);
                    } else {
                        std::string scheme = use_ssl ? "https" : "http";
                        redirect_url = scheme + "://" + host + "/" + std::string(location);
                    }
                }
                
                redirect_req.set_uri(redirect_url);
                
                co_return co_await send_with_redirects(redirect_req, redirect_count + 1);
            }
        }
    }

    co_return result;
}

// =============================================================================
// Main Send Methods
// =============================================================================

auto client::send(const request& req)
    -> task<std::expected<response, std::error_code>>
{
    co_return co_await send_with_redirects(req, 0);
}

auto client::send(http_method method, std::string_view url, std::string_view body)
    -> task<std::expected<response, std::error_code>>
{
    request req(method, url);
    
    if (!body.empty()) {
        req.set_body(std::string(body));
        if (req.get_header("Content-Type").empty()) {
            req.set_header("Content-Type", "application/octet-stream");
        }
    }

    co_return co_await send(req);
}

auto client::send_batch(std::span<const request> requests)
    -> task<std::vector<std::expected<response, std::error_code>>>
{
    std::vector<std::expected<response, std::error_code>> results;
    if (requests.empty()) co_return results;

    const auto first_uri = requests.front().uri();
    std::string host;
    std::uint16_t port{};
    bool use_ssl{};
    if (first_uri.starts_with("http://") || first_uri.starts_with("https://")) {
        const auto parsed = url::parse(first_uri);
        if (!parsed) {
            for (std::size_t i = 0; i < requests.size(); ++i)
                results.emplace_back(std::unexpected(make_error_code(http_errc::invalid_uri)));
            co_return results;
        }
        host = parsed->host;
        port = parsed->port;
        use_ssl = parsed->scheme == "https";
        const auto connected = co_await connect(host, port, use_ssl);
        if (!connected) {
            for (std::size_t i = 0; i < requests.size(); ++i)
                results.emplace_back(std::unexpected(connected.error()));
            co_return results;
        }
    } else if (state_ && state_->conn && state_->conn->is_open()) {
        host = state_->host;
        port = state_->port;
        use_ssl = state_->is_ssl;
    } else {
        for (std::size_t i = 0; i < requests.size(); ++i)
            results.emplace_back(std::unexpected(make_error_code(std::errc::not_connected)));
        co_return results;
    }

    for (const auto& request : requests) {
        const auto uri = request.uri();
        if (!uri.starts_with("http://") && !uri.starts_with("https://")) continue;
        const auto parsed = url::parse(uri);
        if (!parsed || parsed->host != host || parsed->port != port ||
            (parsed->scheme == "https") != use_ssl) {
            for (std::size_t i = 0; i < requests.size(); ++i)
                results.emplace_back(std::unexpected(make_error_code(http_errc::invalid_uri)));
            co_return results;
        }
    }

    if (state_->protocol == protocol_type::http2) {
        co_return co_await send_http2_batch(requests);
    }
    results.reserve(requests.size());
    for (const auto& request : requests) {
        results.emplace_back(co_await send_http1(request));
    }
    co_return results;
}

} // namespace cnetmod::http
