module;

module cnetmod.protocol.socks5;

import std;
import :types;
import :server;
import cnetmod.core.socket;
import cnetmod.core.address;
import cnetmod.core.buffer;
import cnetmod.core.error;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.executor.async_op;
import cnetmod.executor.pool;
import cnetmod.protocol.tcp;

namespace cnetmod::socks5 {

auto server::listen(std::string_view host, std::uint16_t port)
    -> std::expected<void, std::error_code> {
    
    auto addr_r = ip_address::from_string(host);
    if (!addr_r) return std::unexpected(addr_r.error());
    
    acceptor_ = std::make_unique<tcp::acceptor>(ctx_);
    auto ep = endpoint{*addr_r, port};
    auto r = acceptor_->open(ep, {.reuse_address = true});
    if (!r) return std::unexpected(r.error());
    
    return {};
}

auto server::run() -> task<void> {
    running_ = true;
    
    while (running_) {
        auto r = co_await async_accept(ctx_, acceptor_->native_socket());
        if (!r) {
            if (!running_) break;
            continue;
        }
        
        // Connection limit check
        if (config_.max_connections > 0 &&
            active_connections_.load(std::memory_order_relaxed) >= config_.max_connections) {
            r->close();
            continue;
        }
        
        if (sctx_) {
            // Multi-core mode: dispatch to worker io_context
            auto& worker = sctx_->next_worker_io();
            spawn_on(worker, handle_connection(std::move(*r), worker));
        } else {
            // Single-threaded mode
            spawn(ctx_, handle_connection(std::move(*r), ctx_));
        }
    }
}

void server::stop() {
    running_ = false;
    if (acceptor_) acceptor_->close();
}

auto server::handle_connection(socket client, io_context& io) -> task<void> {
    conn_count_guard cg(active_connections_);
    
    // Handle authentication
    auto auth_r = co_await handle_authentication(client, io);
    if (!auth_r) {
        client.close();
        co_return;
    }
    
    // Handle request
    auto req_r = co_await handle_request(client, io);
    if (!req_r) {
        client.close();
        co_return;
    }
    
    // Connection established, relay will continue until closed
}

auto server::handle_authentication(socket& client, io_context& io) 
    -> task<std::expected<void, std::error_code>> {
    
    // Receive authentication method negotiation
    std::array<std::byte, 257> auth_buf;
    auto read_r = co_await async_read(io, client,
        mutable_buffer{auth_buf.data(), auth_buf.size()});
    if (!read_r || *read_r < 2) {
        co_return std::unexpected(make_error_code(std::errc::protocol_error));
    }
    
    auto auth_req = auth_request::parse(auth_buf.data(), *read_r);
    if (!auth_req) {
        co_return std::unexpected(make_error_code(std::errc::protocol_error));
    }
    
    // Select authentication method
    auth_method selected = auth_method::no_acceptable;
    
    for (auto method : auth_req->methods) {
        if (method == auth_method::no_auth && config_.allow_no_auth) {
            selected = auth_method::no_auth;
            break;
        }
        if (method == auth_method::username_password && 
            config_.allow_username_password && config_.authenticator) {
            selected = auth_method::username_password;
            break;
        }
    }
    
    // Send authentication method selection
    auth_response auth_resp;
    auth_resp.method = selected;
    auto auth_data = auth_resp.serialize();
    
    auto write_r = co_await async_write(io, client,
        const_buffer{auth_data.data(), auth_data.size()});
    if (!write_r) {
        co_return std::unexpected(write_r.error());
    }
    
    if (selected == auth_method::no_acceptable) {
        co_return std::unexpected(make_error_code(std::errc::permission_denied));
    }
    
    // Handle username/password authentication
    if (selected == auth_method::username_password) {
        std::array<std::byte, 513> up_buf;
        auto up_read_r = co_await async_read(io, client,
            mutable_buffer{up_buf.data(), up_buf.size()});
        if (!up_read_r || *up_read_r < 3) {
            co_return std::unexpected(make_error_code(std::errc::protocol_error));
        }
        
        auto up_req = username_password_request::parse(up_buf.data(), *up_read_r);
        if (!up_req) {
            co_return std::unexpected(make_error_code(std::errc::protocol_error));
        }
        
        // Authenticate
        bool authenticated = config_.authenticator(up_req->username, up_req->password);
        
        username_password_response up_resp;
        up_resp.status = authenticated ? 0x00 : 0x01;
        auto up_data = up_resp.serialize();
        
        auto up_write_r = co_await async_write(io, client,
            const_buffer{up_data.data(), up_data.size()});
        if (!up_write_r) {
            co_return std::unexpected(up_write_r.error());
        }
        
        if (!authenticated) {
            co_return std::unexpected(make_error_code(std::errc::permission_denied));
        }
    }
    
    co_return {};
}

auto server::handle_request(socket& client, io_context& io) 
    -> task<std::expected<void, std::error_code>> {
    
    // Receive SOCKS5 request
    std::array<std::byte, 512> req_buf;
    auto read_r = co_await async_read(io, client,
        mutable_buffer{req_buf.data(), req_buf.size()});
    if (!read_r || *read_r < 4) {
        co_return std::unexpected(make_error_code(std::errc::protocol_error));
    }
    
    auto req = socks5_request::parse(req_buf.data(), *read_r);
    if (!req) {
        co_return std::unexpected(make_error_code(std::errc::protocol_error));
    }
    
    // Only support CONNECT command
    if (req->cmd != command::connect) {
        socks5_response resp;
        resp.rep = reply::command_not_supported;
        resp.bind_address.type = address_type::ipv4;
        resp.bind_address.host = "0.0.0.0";
        resp.bind_address.port = 0;
        
        auto resp_data = resp.serialize();
        co_await async_write(io, client,
            const_buffer{resp_data.data(), resp_data.size()});
        
        co_return std::unexpected(make_error_code(std::errc::not_supported));
    }
    
    // Connect to target
    auto target_r = socket::create(address_family::ipv4, socket_type::stream);
    if (!target_r) {
        socks5_response resp;
        resp.rep = reply::general_failure;
        resp.bind_address.type = address_type::ipv4;
        resp.bind_address.host = "0.0.0.0";
        resp.bind_address.port = 0;
        
        auto resp_data = resp.serialize();
        co_await async_write(io, client,
            const_buffer{resp_data.data(), resp_data.size()});
        co_return std::unexpected(target_r.error());
    }
    socket target = std::move(*target_r);
    
    auto nb_r = target.set_non_blocking(true);
    if (!nb_r) {
        socks5_response resp;
        resp.rep = reply::general_failure;
        resp.bind_address.type = address_type::ipv4;
        resp.bind_address.host = "0.0.0.0";
        resp.bind_address.port = 0;
        
        auto resp_data = resp.serialize();
        co_await async_write(io, client,
            const_buffer{resp_data.data(), resp_data.size()});
        co_return std::unexpected(nb_r.error());
    }
    
    // Resolve target address
    std::expected<ip_address, std::error_code> target_addr;
    
    if (req->address.type == address_type::domain_name) {
        // TODO: DNS resolution
        // For now, try to parse as IP
        target_addr = ip_address::from_string(req->address.host);
    } else {
        target_addr = ip_address::from_string(req->address.host);
    }
    
    if (!target_addr) {
        socks5_response resp;
        resp.rep = reply::host_unreachable;
        resp.bind_address.type = address_type::ipv4;
        resp.bind_address.host = "0.0.0.0";
        resp.bind_address.port = 0;
        
        auto resp_data = resp.serialize();
        co_await async_write(io, client,
            const_buffer{resp_data.data(), resp_data.size()});
        
        co_return std::unexpected(target_addr.error());
    }
    
    endpoint target_ep{*target_addr, req->address.port};
    auto conn_r = co_await async_connect(io, target, target_ep);
    
    // Send response
    socks5_response resp;
    if (conn_r) {
        resp.rep = reply::succeeded;
        resp.bind_address.type = address_type::ipv4;
        resp.bind_address.host = "0.0.0.0";
        resp.bind_address.port = 0;
    } else {
        // Map error to SOCKS5 reply
        if (conn_r.error() == std::errc::connection_refused) {
            resp.rep = reply::connection_refused;
        } else if (conn_r.error() == std::errc::network_unreachable) {
            resp.rep = reply::network_unreachable;
        } else if (conn_r.error() == std::errc::host_unreachable) {
            resp.rep = reply::host_unreachable;
        } else {
            resp.rep = reply::general_failure;
        }
        resp.bind_address.type = address_type::ipv4;
        resp.bind_address.host = "0.0.0.0";
        resp.bind_address.port = 0;
    }
    
    auto resp_data = resp.serialize();
    auto write_r = co_await async_write(io, client,
        const_buffer{resp_data.data(), resp_data.size()});
    if (!write_r) {
        co_return std::unexpected(write_r.error());
    }
    
    if (!conn_r) {
        co_return std::unexpected(conn_r.error());
    }
    
    // Start bidirectional relay
    co_await relay_data(client, target, io);
    
    co_return {};
}

auto server::relay_data(socket& client, socket& target, io_context& io) -> task<void> {
    // Simple bidirectional relay
    // In production, this should use two concurrent tasks
    
    std::array<std::byte, 8192> buf;
    
    while (true) {
        // Read from client
        auto client_read = co_await async_read(io, client,
            mutable_buffer{buf.data(), buf.size()});
        if (!client_read || *client_read == 0) break;
        
        // Write to target
        auto target_write = co_await async_write(io, target,
            const_buffer{buf.data(), *client_read});
        if (!target_write) break;
        
        // Read from target
        auto target_read = co_await async_read(io, target,
            mutable_buffer{buf.data(), buf.size()});
        if (!target_read || *target_read == 0) break;
        
        // Write to client
        auto client_write = co_await async_write(io, client,
            const_buffer{buf.data(), *target_read});
        if (!client_write) break;
    }
    
    client.close();
    target.close();
}

} // namespace cnetmod::socks5
