module;

#include <cnetmod/config.hpp>

#ifdef CNETMOD_HAS_NGHTTP2
#define NGHTTP2_NO_SSIZE_T
#include <nghttp2/nghttp2.h>
#endif

module cnetmod.protocol.http;

import std;
import :types;
import :request;
import :response;
import :parser;
import :cookie;
import :client;
import cnetmod.core.error;
import cnetmod.core.buffer;
import cnetmod.core.socket;
import cnetmod.core.address;
import cnetmod.io.io_context;
import cnetmod.protocol.tcp;
import cnetmod.coro.task;
import cnetmod.executor.async_op;

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
#ifdef CNETMOD_HAS_NGHTTP2
        ssl_ctx_->configure_alpn_client({"h2"});
#endif
        break;
    case http_version_preference::http1_only:
        ssl_ctx_->configure_alpn_client({"http/1.1"});
        break;
    case http_version_preference::http2_preferred:
#ifdef CNETMOD_HAS_NGHTTP2
        ssl_ctx_->configure_alpn_client({"h2", "http/1.1"});
#else
        ssl_ctx_->configure_alpn_client({"http/1.1"});
#endif
        break;
    case http_version_preference::http1_preferred:
#ifdef CNETMOD_HAS_NGHTTP2
        ssl_ctx_->configure_alpn_client({"http/1.1", "h2"});
#else
        ssl_ctx_->configure_alpn_client({"http/1.1"});
#endif
        break;
    }
}
#endif

// =============================================================================
// Connection Management
// =============================================================================

void client::close() noexcept {
    if (!state_) return;
    
#ifdef CNETMOD_HAS_NGHTTP2
    if (state_->h2_session) {
        nghttp2_session_del(state_->h2_session);
        state_->h2_session = nullptr;
    }
#endif
    
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

    // Parse host to endpoint (simple IP parsing, TODO: DNS resolution)
    auto addr_result = ip_address::from_string(host);
    endpoint ep;
    
    if (addr_result) {
        ep = endpoint{*addr_result, port};
    } else {
        // Fallback to loopback for now (TODO: implement DNS)
        ep = endpoint{ip_address{ipv4_address::loopback()}, port};
    }

    // Create socket
    auto family = ep.address().is_v6() ? address_family::ipv6 : address_family::ipv4;
    auto sock_result = socket::create(family, socket_type::stream);
    if (!sock_result) {
        co_return std::unexpected(sock_result.error());
    }
    
    auto sock = std::move(*sock_result);

    // Set socket options
    socket_options opts;
    opts.reuse_address = true;
    opts.non_blocking = true;
    auto opts_result = sock.apply_options(opts);
    if (!opts_result) {
        co_return std::unexpected(opts_result.error());
    }

    // Async connect
    auto connect_result = co_await async_connect(*ctx_, sock, ep);
    if (!connect_result) {
        co_return std::unexpected(connect_result.error());
    }

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
#ifdef CNETMOD_HAS_NGHTTP2
            state_->protocol = protocol_type::http2;
            auto init_result = co_await init_h2_session();
            if (!init_result) {
                close();
                co_return std::unexpected(init_result.error());
            }
#else
            close();
            co_return std::unexpected(make_error_code(std::errc::not_supported));
#endif
        } else {
            state_->protocol = protocol_type::http1;
        }
    } else
#endif
    {
        // Plain HTTP - check version preference
        switch (options_.version_pref) {
        case http_version_preference::http2_only: {
#ifdef CNETMOD_HAS_NGHTTP2
            state_->protocol = protocol_type::http2;
            auto init_result = co_await init_h2_session();
            if (!init_result) {
                close();
                co_return std::unexpected(init_result.error());
            }
#else
            close();
            co_return std::unexpected(make_error_code(std::errc::not_supported));
#endif
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
        std::size_t total = 0;
        while (total < data.size()) {
            auto result = co_await state_->ssl->async_write(
                const_buffer{data.data() + total, data.size() - total});
            if (!result) {
                co_return std::unexpected(result.error());
            }
            total += *result;
        }
        co_return {};
    }
#endif

    auto& sock = state_->conn->native_socket();
    std::size_t total = 0;

    while (total < data.size()) {
        auto result = co_await async_write(*ctx_, sock,
            const_buffer{data.data() + total, data.size() - total});
        if (!result) {
            co_return std::unexpected(result.error());
        }
        total += *result;
    }

    co_return {};
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

    // Build request with proper headers
    request modified_req = req;
    
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
    
    modified_req.set_uri(path);
    
    // Set required headers
    if (modified_req.get_header("Host").empty()) {
        if (state_->port == 80 || state_->port == 443) {
            modified_req.set_header("Host", state_->host);
        } else {
            modified_req.set_header("Host", 
                state_->host + ":" + std::to_string(state_->port));
        }
    }
    
    if (modified_req.get_header("User-Agent").empty()) {
        modified_req.set_header("User-Agent", options_.user_agent);
    }
    
    if (modified_req.get_header("Connection").empty()) {
        modified_req.set_header("Connection", 
            options_.keep_alive ? "keep-alive" : "close");
    }
    
    // 添加 Cookies
    if (options_.enable_cookies && modified_req.get_header("Cookie").empty()) {
        auto cookie_header = cookies_.to_cookie_header(
            state_->host, path, state_->is_ssl);
        if (!cookie_header.empty()) {
            modified_req.set_header("Cookie", cookie_header);
        }
    }

    // Send request
    auto request_data = modified_req.serialize();
    auto send_result = co_await write_data(request_data);
    if (!send_result) {
        co_return std::unexpected(send_result.error());
    }

    // Receive response - read until we have complete headers
    std::string buffer;
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
    std::string body;
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
        // Handle chunked encoding - 完整实现
        std::string remaining_data(view);
        
        while (true) {
            // 查找 chunk size 行
            auto crlf_pos = remaining_data.find("\r\n");
            
            // 如果没有完整的行，读取更多数据
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
            
            // 解析 chunk size (十六进制)
            auto size_str = remaining_data.substr(0, crlf_pos);
            
            // 移除可能的扩展（分号后的内容）
            auto semicolon = size_str.find(';');
            if (semicolon != std::string::npos) {
                size_str = size_str.substr(0, semicolon);
            }
            
            std::size_t chunk_size = 0;
            auto [ptr, ec] = std::from_chars(
                size_str.data(), 
                size_str.data() + size_str.size(), 
                chunk_size, 16);  // 十六进制
            
            if (ec != std::errc{}) {
                co_return std::unexpected(make_error_code(http_errc::invalid_chunk));
            }
            
            // chunk size 为 0 表示结束
            if (chunk_size == 0) {
                // 读取 trailer headers（如果有）
                remaining_data = remaining_data.substr(crlf_pos + 2);
                
                // 跳过 trailer 直到遇到空行
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
                        // 空行，结束
                        break;
                    }
                    
                    // 跳过这一行
                    remaining_data = remaining_data.substr(trailer_crlf + 2);
                }
                
                break;  // 所有 chunks 读取完毕
            }
            
            // 移除 size 行
            remaining_data = remaining_data.substr(crlf_pos + 2);
            
            // 确保有足够的数据（chunk data + \r\n）
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
            
            // 提取 chunk 数据
            body.append(remaining_data.substr(0, chunk_size));
            
            // 移除 chunk 数据和结尾的 \r\n
            remaining_data = remaining_data.substr(chunk_size + 2);
        }
    }

    resp.set_body(std::move(body));

    // 处理 Set-Cookie 头
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

#ifdef CNETMOD_HAS_NGHTTP2

// =============================================================================
// HTTP/2 Session Initialization
// =============================================================================

auto client::init_h2_session() -> task<std::expected<void, std::error_code>> {
    if (!state_ || state_->h2_session) {
        co_return {};
    }

    // Create callbacks
    nghttp2_session_callbacks* callbacks = nullptr;
    nghttp2_session_callbacks_new(&callbacks);

    nghttp2_session_callbacks_set_on_header_callback(
        callbacks, h2_on_header_callback);
    nghttp2_session_callbacks_set_on_data_chunk_recv_callback(
        callbacks, h2_on_data_chunk_recv_callback);
    nghttp2_session_callbacks_set_on_frame_recv_callback(
        callbacks, h2_on_frame_recv_callback);
    nghttp2_session_callbacks_set_on_stream_close_callback(
        callbacks, h2_on_stream_close_callback);

    // Create client session
    int rv = nghttp2_session_client_new(&state_->h2_session, callbacks, this);
    nghttp2_session_callbacks_del(callbacks);

    if (rv != 0) {
        co_return std::unexpected(make_error_code(h2_errc::internal_error));
    }

    // Submit client settings
    nghttp2_settings_entry iv[] = {
        {NGHTTP2_SETTINGS_MAX_CONCURRENT_STREAMS, 
         options_.h2_max_concurrent_streams},
        {NGHTTP2_SETTINGS_INITIAL_WINDOW_SIZE, 
         options_.h2_initial_window_size},
        {NGHTTP2_SETTINGS_ENABLE_PUSH, 0},
    };

    rv = nghttp2_submit_settings(
        state_->h2_session, NGHTTP2_FLAG_NONE,
        iv, sizeof(iv) / sizeof(iv[0]));

    if (rv != 0) {
        nghttp2_session_del(state_->h2_session);
        state_->h2_session = nullptr;
        co_return std::unexpected(make_error_code(h2_errc::internal_error));
    }

    // Flush settings
    std::string buffer;
    for (;;) {
        const uint8_t* data = nullptr;
        auto len = nghttp2_session_mem_send2(state_->h2_session, &data);
        if (len < 0) {
            co_return std::unexpected(make_error_code(h2_errc::internal_error));
        }
        if (len == 0) break;
        buffer.append(reinterpret_cast<const char*>(data), 
                     static_cast<std::size_t>(len));
    }

    if (!buffer.empty()) {
        auto result = co_await write_data(buffer);
        if (!result) {
            co_return std::unexpected(result.error());
        }
    }

    // Receive server settings
    char temp[16384];
    auto result = co_await read_data(temp, sizeof(temp));
    if (!result) {
        co_return std::unexpected(result.error());
    }

    auto consumed = nghttp2_session_mem_recv2(
        state_->h2_session,
        reinterpret_cast<const uint8_t*>(temp),
        *result);

    if (consumed < 0) {
        co_return std::unexpected(make_error_code(h2_errc::protocol_error));
    }

    co_return {};
}

// =============================================================================
// HTTP/2 Send Implementation
// =============================================================================

auto client::send_http2(const request& req)
    -> task<std::expected<response, std::error_code>>
{
    if (!state_ || !state_->h2_session) {
        co_return std::unexpected(make_error_code(std::errc::not_connected));
    }

    // Parse URI to get path
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

    // Allocate stream ID
    std::int32_t stream_id = state_->next_stream_id;
    state_->next_stream_id += 2;

    // Create stream data
    state_->h2_streams[stream_id] = connection_state::h2_stream_data{};

    // Build HTTP/2 headers
    std::vector<nghttp2_nv> nva;
    nva.reserve(req.headers().size() + 5);

    // Pseudo-headers
    auto method_str = std::string(method_to_string(req.method()));
    nva.push_back({
        const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(":method")),
        const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(method_str.data())),
        7, method_str.size(),
        NGHTTP2_NV_FLAG_NO_COPY_NAME
    });

    nva.push_back({
        const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(":path")),
        const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(path.data())),
        5, path.size(),
        NGHTTP2_NV_FLAG_NO_COPY_NAME
    });

    std::string scheme = state_->is_ssl ? "https" : "http";
    nva.push_back({
        const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(":scheme")),
        const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(scheme.data())),
        7, scheme.size(),
        NGHTTP2_NV_FLAG_NO_COPY_NAME
    });

    // Authority
    std::string authority;
    auto host_header = req.get_header("Host");
    if (!host_header.empty()) {
        authority = std::string(host_header);
    } else {
        authority = state_->host;
        if ((state_->is_ssl && state_->port != 443) || 
            (!state_->is_ssl && state_->port != 80)) {
            authority += ":";
            authority += std::to_string(state_->port);
        }
    }

    nva.push_back({
        const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(":authority")),
        const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(authority.data())),
        10, authority.size(),
        NGHTTP2_NV_FLAG_NO_COPY_NAME
    });

    // Regular headers (convert to lowercase, filter hop-by-hop)
    for (const auto& [key, value] : req.headers()) {
        if (key.empty() || key[0] == ':') continue;
        if (key == "Host" || key == "Connection" || key == "Keep-Alive" ||
            key == "Transfer-Encoding" || key == "Upgrade") {
            continue;
        }

        std::string lower_key(key);
        std::ranges::transform(lower_key, lower_key.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

        nva.push_back({
            const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(lower_key.data())),
            const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(value.data())),
            lower_key.size(), value.size(),
            NGHTTP2_NV_FLAG_NO_COPY_NAME | NGHTTP2_NV_FLAG_NO_COPY_VALUE
        });
    }

    // Submit request
    int rv = nghttp2_submit_request2(
        state_->h2_session,
        nullptr,
        nva.data(),
        nva.size(),
        nullptr,
        nullptr
    );

    if (rv < 0) {
        state_->h2_streams.erase(stream_id);
        co_return std::unexpected(make_error_code(h2_errc::internal_error));
    }

    // Flush request
    std::string buffer;
    for (;;) {
        const uint8_t* data = nullptr;
        auto len = nghttp2_session_mem_send2(state_->h2_session, &data);
        if (len < 0) {
            state_->h2_streams.erase(stream_id);
            co_return std::unexpected(make_error_code(h2_errc::internal_error));
        }
        if (len == 0) break;
        buffer.append(reinterpret_cast<const char*>(data), 
                     static_cast<std::size_t>(len));
    }

    if (!buffer.empty()) {
        auto result = co_await write_data(buffer);
        if (!result) {
            state_->h2_streams.erase(stream_id);
            co_return std::unexpected(result.error());
        }
    }

    // Receive response
    while (!state_->h2_streams[stream_id].complete) {
        char temp[16384];
        auto result = co_await read_data(temp, sizeof(temp));
        if (!result) {
            state_->h2_streams.erase(stream_id);
            co_return std::unexpected(result.error());
        }

        if (*result == 0) {
            state_->h2_streams.erase(stream_id);
            co_return std::unexpected(make_error_code(std::errc::connection_reset));
        }

        auto consumed = nghttp2_session_mem_recv2(
            state_->h2_session,
            reinterpret_cast<const uint8_t*>(temp),
            *result);

        if (consumed < 0) {
            state_->h2_streams.erase(stream_id);
            co_return std::unexpected(make_error_code(h2_errc::protocol_error));
        }

        // Flush any pending frames
        buffer.clear();
        for (;;) {
            const uint8_t* data = nullptr;
            auto len = nghttp2_session_mem_send2(state_->h2_session, &data);
            if (len < 0) {
                state_->h2_streams.erase(stream_id);
                co_return std::unexpected(make_error_code(h2_errc::internal_error));
            }
            if (len == 0) break;
            buffer.append(reinterpret_cast<const char*>(data), 
                         static_cast<std::size_t>(len));
        }

        if (!buffer.empty()) {
            auto wr = co_await write_data(buffer);
            if (!wr) {
                state_->h2_streams.erase(stream_id);
                co_return std::unexpected(wr.error());
            }
        }
    }

    // Build response
    auto& stream = state_->h2_streams[stream_id];
    response resp(stream.status_code);
    for (const auto& [key, value] : stream.headers) {
        resp.set_header(key, value);
    }
    resp.set_body(std::move(stream.body));

    state_->h2_streams.erase(stream_id);
    co_return resp;
}

// =============================================================================
// HTTP/2 Callbacks
// =============================================================================

auto client::h2_on_header_callback(
    nghttp2_session* /*session*/,
    const nghttp2_frame* frame,
    const uint8_t* name, size_t namelen,
    const uint8_t* value, size_t valuelen,
    uint8_t /*flags*/,
    void* user_data) -> int
{
    auto* self = static_cast<client*>(user_data);
    
    if (frame->hd.type != NGHTTP2_HEADERS || !self->state_) {
        return 0;
    }

    auto it = self->state_->h2_streams.find(frame->hd.stream_id);
    if (it == self->state_->h2_streams.end()) {
        return 0;
    }

    auto& stream = it->second;
    std::string_view name_sv(reinterpret_cast<const char*>(name), namelen);
    std::string_view value_sv(reinterpret_cast<const char*>(value), valuelen);

    if (name_sv == ":status") {
        auto [ptr, ec] = std::from_chars(value_sv.data(),
                                        value_sv.data() + value_sv.size(),
                                        stream.status_code);
        if (ec != std::errc{}) {
            return NGHTTP2_ERR_CALLBACK_FAILURE;
        }
    } else if (!name_sv.empty() && name_sv[0] != ':') {
        stream.headers[std::string(name_sv)] = std::string(value_sv);
    }

    return 0;
}

auto client::h2_on_data_chunk_recv_callback(
    nghttp2_session* session,
    uint8_t /*flags*/,
    int32_t stream_id,
    const uint8_t* data, size_t len,
    void* user_data) -> int
{
    auto* self = static_cast<client*>(user_data);
    
    if (!self->state_) return 0;
    
    auto it = self->state_->h2_streams.find(stream_id);
    if (it != self->state_->h2_streams.end()) {
        it->second.body.append(reinterpret_cast<const char*>(data), len);
    }

    nghttp2_session_consume(session, stream_id, len);

    return 0;
}

auto client::h2_on_frame_recv_callback(
    nghttp2_session* /*session*/,
    const nghttp2_frame* frame,
    void* user_data) -> int
{
    auto* self = static_cast<client*>(user_data);

    if (!self->state_) return 0;

    if (frame->hd.type == NGHTTP2_HEADERS) {
        auto it = self->state_->h2_streams.find(frame->hd.stream_id);
        if (it != self->state_->h2_streams.end()) {
            if (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) {
                it->second.complete = true;
            }
        }
    } else if (frame->hd.type == NGHTTP2_DATA) {
        if (frame->hd.flags & NGHTTP2_FLAG_END_STREAM) {
            auto it = self->state_->h2_streams.find(frame->hd.stream_id);
            if (it != self->state_->h2_streams.end()) {
                it->second.complete = true;
            }
        }
    }

    return 0;
}

auto client::h2_on_stream_close_callback(
    nghttp2_session* /*session*/,
    int32_t stream_id,
    uint32_t /*error_code*/,
    void* user_data) -> int
{
    auto* self = static_cast<client*>(user_data);
    
    if (!self->state_) return 0;
    
    auto it = self->state_->h2_streams.find(stream_id);
    if (it != self->state_->h2_streams.end()) {
        it->second.complete = true;
    }

    return 0;
}

#endif // CNETMOD_HAS_NGHTTP2

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

    // Connect if needed
    if (!host.empty()) {
        auto connect_result = co_await connect(host, port, use_ssl);
        if (!connect_result) {
            co_return std::unexpected(connect_result.error());
        }
    }

    // Dispatch based on protocol
    std::expected<response, std::error_code> result;
    
    if (state_->protocol == protocol_type::http1) {
        result = co_await send_http1(req);
    }
#ifdef CNETMOD_HAS_NGHTTP2
    else if (state_->protocol == protocol_type::http2) {
        result = co_await send_http2(req);
    }
#endif
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
                    redirect_url = scheme + "://" + host;
                    if ((use_ssl && port != 443) || (!use_ssl && port != 80)) {
                        redirect_url += ":" + std::to_string(port);
                    }
                    redirect_url += std::string(location);
                } else {
                    auto current_url = url::parse(uri);
                    if (current_url) {
                        std::string scheme = current_url->scheme;
                        redirect_url = scheme + "://" + current_url->host;
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

} // namespace cnetmod::http
