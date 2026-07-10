module;

module cnetmod.protocol.socks5;

import std;
import :types;
import :client;
import cnetmod.core.socket;
import cnetmod.core.address;
import cnetmod.core.buffer;
import cnetmod.core.error;
import cnetmod.core.dns;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.executor.async_op;

namespace cnetmod::socks5 {

auto client::connect(std::string_view proxy_host, std::uint16_t proxy_port)
    -> task<std::expected<void, std::error_code>> {
    
    auto connect_r = co_await async_connect_happy_eyeballs(ctx_, proxy_host, proxy_port);
    if (!connect_r) {
        co_return std::unexpected(connect_r.error());
    }
    sock_ = std::move(connect_r->sock);
    
    // Send authentication method negotiation
    auth_request auth_req;
    auth_req.methods = {auth_method::no_auth, auth_method::username_password};
    
    auto auth_data = auth_req.serialize();
    auto write_r = co_await async_write_all(ctx_, sock_,
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
    auto write_r = co_await async_write_all(ctx_, sock_,
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
    auto resp = co_await request(command::connect, target_host, target_port);
    if (!resp) {
        co_return std::unexpected(resp.error());
    }
    co_return {};
}

auto client::bind(std::string_view target_host, std::uint16_t target_port)
    -> task<std::expected<socks5_address, std::error_code>> {
    auto resp = co_await request(command::bind, target_host, target_port);
    if (!resp) {
        co_return std::unexpected(resp.error());
    }
    co_return resp->bind_address;
}

auto client::wait_bind_peer()
    -> task<std::expected<socks5_address, std::error_code>> {
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
        co_return std::unexpected(make_error_code(std::errc::protocol_error));
    }

    co_return resp->bind_address;
}

auto client::udp_associate(std::string_view client_host, std::uint16_t client_port)
    -> task<std::expected<socks5_address, std::error_code>> {
    auto resp = co_await request(command::udp_associate, client_host, client_port);
    if (!resp) {
        co_return std::unexpected(resp.error());
    }
    co_return resp->bind_address;
}

auto client::request(command cmd, std::string_view host, std::uint16_t port)
    -> task<std::expected<socks5_response, std::error_code>> {
    // Build SOCKS5 request
    socks5_request req;
    req.cmd = cmd;
    
    // Determine address type
    auto addr_r = ip_address::from_string(host);
    if (addr_r) {
        // It's an IP address
        if (addr_r->is_v4()) {
            req.address.type = address_type::ipv4;
        } else {
            req.address.type = address_type::ipv6;
        }
        req.address.host = std::string(host);
    } else {
        // It's a domain name
        req.address.type = address_type::domain_name;
        req.address.host = std::string(host);
    }
    req.address.port = port;
    
    // Send request
    auto req_data = req.serialize();
    auto write_r = co_await async_write_all(ctx_, sock_,
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
    
    co_return *resp;
}

} // namespace cnetmod::socks5
