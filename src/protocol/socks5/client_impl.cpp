module;

module cnetmod.protocol.socks5;

import std;
import cnetmod.core.socket;
import cnetmod.core.address;
import cnetmod.core.buffer;
import cnetmod.core.error;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.executor.async_op;

namespace cnetmod::socks5 {

auto client::connect(std::string_view proxy_host, std::uint16_t proxy_port)
    -> task<std::expected<void, std::error_code>> {
    
    // Resolve proxy address
    auto addr_r = ip_address::from_string(proxy_host);
    if (!addr_r) {
        co_return std::unexpected(addr_r.error());
    }
    
    // Create socket
    auto sock_r = socket::create(address_family::ipv4, socket_type::stream);
    if (!sock_r) {
        co_return std::unexpected(sock_r.error());
    }
    sock_ = std::move(*sock_r);
    
    // Set non-blocking
    auto nb_r = sock_.set_non_blocking(true);
    if (!nb_r) {
        co_return std::unexpected(nb_r.error());
    }
    
    // Connect to proxy
    endpoint ep{*addr_r, proxy_port};
    auto conn_r = co_await async_connect(ctx_, sock_, ep);
    if (!conn_r) {
        co_return std::unexpected(conn_r.error());
    }
    
    // Send authentication method negotiation
    auth_request auth_req;
    auth_req.methods = {auth_method::no_auth, auth_method::username_password};
    
    auto auth_data = auth_req.serialize();
    auto write_r = co_await async_write(ctx_, sock_,
        const_buffer{auth_data.data(), auth_data.size()});
    if (!write_r) {
        co_return std::unexpected(write_r.error());
    }
    
    // Receive authentication method selection
    std::array<std::byte, 2> auth_resp_buf;
    auto read_r = co_await async_read(ctx_, sock_,
        mutable_buffer{auth_resp_buf.data(), auth_resp_buf.size()});
    if (!read_r || *read_r != 2) {
        co_return std::unexpected(make_error_code(std::errc::protocol_error));
    }
    
    auto auth_resp = auth_response::parse(auth_resp_buf.data(), *read_r);
    if (!auth_resp) {
        co_return std::unexpected(make_error_code(std::errc::protocol_error));
    }
    
    if (auth_resp->method == auth_method::no_acceptable) {
        co_return std::unexpected(make_error_code(std::errc::permission_denied));
    }
    
    selected_auth_ = auth_resp->method;
    
    co_return {};
}

auto client::authenticate(std::string_view username, std::string_view password)
    -> task<std::expected<void, std::error_code>> {
    
    if (selected_auth_ != auth_method::username_password) {
        // No authentication needed
        co_return {};
    }
    
    // Send username/password
    username_password_request up_req;
    up_req.username = std::string(username);
    up_req.password = std::string(password);
    
    auto up_data = up_req.serialize();
    auto write_r = co_await async_write(ctx_, sock_,
        const_buffer{up_data.data(), up_data.size()});
    if (!write_r) {
        co_return std::unexpected(write_r.error());
    }
    
    // Receive authentication response
    std::array<std::byte, 2> up_resp_buf;
    auto read_r = co_await async_read(ctx_, sock_,
        mutable_buffer{up_resp_buf.data(), up_resp_buf.size()});
    if (!read_r || *read_r != 2) {
        co_return std::unexpected(make_error_code(std::errc::protocol_error));
    }
    
    auto up_resp = username_password_response::parse(up_resp_buf.data(), *read_r);
    if (!up_resp || up_resp->status != 0x00) {
        co_return std::unexpected(make_error_code(std::errc::permission_denied));
    }
    
    co_return {};
}

auto client::connect_target(std::string_view target_host, std::uint16_t target_port)
    -> task<std::expected<void, std::error_code>> {
    
    // Build SOCKS5 request
    socks5_request req;
    req.cmd = command::connect;
    
    // Determine address type
    auto addr_r = ip_address::from_string(target_host);
    if (addr_r) {
        // It's an IP address
        if (addr_r->is_v4()) {
            req.address.type = address_type::ipv4;
        } else {
            req.address.type = address_type::ipv6;
        }
        req.address.host = std::string(target_host);
    } else {
        // It's a domain name
        req.address.type = address_type::domain_name;
        req.address.host = std::string(target_host);
    }
    req.address.port = target_port;
    
    // Send request
    auto req_data = req.serialize();
    auto write_r = co_await async_write(ctx_, sock_,
        const_buffer{req_data.data(), req_data.size()});
    if (!write_r) {
        co_return std::unexpected(write_r.error());
    }
    
    // Receive response
    std::array<std::byte, 512> resp_buf;
    auto read_r = co_await async_read(ctx_, sock_,
        mutable_buffer{resp_buf.data(), resp_buf.size()});
    if (!read_r || *read_r < 4) {
        co_return std::unexpected(make_error_code(std::errc::protocol_error));
    }
    
    auto resp = socks5_response::parse(resp_buf.data(), *read_r);
    if (!resp) {
        co_return std::unexpected(make_error_code(std::errc::protocol_error));
    }
    
    if (resp->rep != reply::succeeded) {
        // Map SOCKS5 reply to error code
        switch (resp->rep) {
        case reply::connection_refused:
            co_return std::unexpected(make_error_code(std::errc::connection_refused));
        case reply::network_unreachable:
            co_return std::unexpected(make_error_code(std::errc::network_unreachable));
        case reply::host_unreachable:
            co_return std::unexpected(make_error_code(std::errc::host_unreachable));
        default:
            co_return std::unexpected(make_error_code(std::errc::protocol_error));
        }
    }
    
    co_return {};
}

} // namespace cnetmod::socks5
