module;

module cnetmod.protocol.socks5;

import std;
import :types;
import :server;
import cnetmod.core.socket;
import cnetmod.core.address;
import cnetmod.core.buffer;
import cnetmod.core.error;
import cnetmod.core.dns;
import cnetmod.io.io_context;
import cnetmod.coro.task;
import cnetmod.coro.spawn;
import cnetmod.executor.async_op;
import cnetmod.executor.pool;
import cnetmod.protocol.tcp;

namespace cnetmod::socks5 {

namespace {

auto make_any_address_response(reply rep) -> socks5_response {
    socks5_response resp;
    resp.rep = rep;
    resp.bind_address.type = address_type::ipv4;
    resp.bind_address.host = "0.0.0.0";
    resp.bind_address.port = 0;
    return resp;
}

auto make_success_response(socket& target) -> socks5_response {
    auto resp = make_any_address_response(reply::succeeded);

    if (auto local = target.local_endpoint()) {
        resp.bind_address.type = local->address().is_v4()
            ? address_type::ipv4
            : address_type::ipv6;
        resp.bind_address.host = local->address().to_string();
        resp.bind_address.port = local->port();
    }

    return resp;
}

auto send_response(io_context& io, socket& client, const socks5_response& resp)
    -> task<std::expected<void, std::error_code>> {
    auto data = resp.serialize();
    co_return co_await async_write_all(io, client, const_buffer{data.data(), data.size()});
}

auto map_connect_error(const std::error_code& ec) noexcept -> reply {
    if (ec == make_error_code(errc::connection_refused)) {
        return reply::connection_refused;
    }
    if (ec == make_error_code(errc::network_unreachable)) {
        return reply::network_unreachable;
    }
    if (ec == make_error_code(errc::host_unreachable) ||
        ec == make_error_code(errc::host_not_found)) {
        return reply::host_unreachable;
    }
    return reply::general_failure;
}

auto resolve_target(io_context& io, const socks5_address& address)
    -> task<std::expected<endpoint, std::error_code>> {
    if (address.type != address_type::domain_name) {
        auto addr = ip_address::from_string(address.host);
        if (!addr) {
            co_return std::unexpected(addr.error());
        }
        co_return endpoint{*addr, address.port};
    }

    auto resolved = co_await async_resolve(io, address.host, std::to_string(address.port));
    if (!resolved) {
        co_return std::unexpected(make_error_code(errc::host_not_found));
    }

    for (const auto& host : *resolved) {
        auto addr = ip_address::from_string(host);
        if (addr) {
            co_return endpoint{*addr, address.port};
        }
    }

    co_return std::unexpected(make_error_code(errc::host_not_found));
}

auto create_stream_socket(const ip_address& address)
    -> std::expected<socket, std::error_code> {
    return socket::create(
        address.is_v4() ? address_family::ipv4 : address_family::ipv6,
        socket_type::stream);
}

auto relay_one_direction(io_context& io, socket& from, socket& to) -> task<void> {
    std::array<std::byte, 8192> buf;

    while (true) {
        auto read_r = co_await async_read(io, from,
            mutable_buffer{buf.data(), buf.size()});
        if (!read_r || *read_r == 0) {
            break;
        }

        auto write_r = co_await async_write_all(io, to,
            const_buffer{buf.data(), *read_r});
        if (!write_r) {
            break;
        }
    }

    from.close();
    to.close();
}

} // namespace

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
    
    auto write_r = co_await async_write_all(io, client,
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
        
        auto up_write_r = co_await async_write_all(io, client,
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
        auto resp = make_any_address_response(reply::command_not_supported);
        if (auto r = co_await send_response(io, client, resp); !r) {
            co_return std::unexpected(r.error());
        }
        
        co_return std::unexpected(make_error_code(std::errc::not_supported));
    }
    
    // Resolve target address
    auto target_ep_r = co_await resolve_target(io, req->address);
    if (!target_ep_r) {
        auto resp = make_any_address_response(map_connect_error(target_ep_r.error()));
        if (auto r = co_await send_response(io, client, resp); !r) {
            co_return std::unexpected(r.error());
        }
        co_return std::unexpected(target_ep_r.error());
    }

    // Connect to target
    auto target_r = create_stream_socket(target_ep_r->address());
    if (!target_r) {
        auto resp = make_any_address_response(reply::general_failure);
        if (auto r = co_await send_response(io, client, resp); !r) {
            co_return std::unexpected(r.error());
        }
        co_return std::unexpected(target_r.error());
    }
    socket target = std::move(*target_r);
    
    auto nb_r = target.set_non_blocking(true);
    if (!nb_r) {
        auto resp = make_any_address_response(reply::general_failure);
        if (auto r = co_await send_response(io, client, resp); !r) {
            co_return std::unexpected(r.error());
        }
        co_return std::unexpected(nb_r.error());
    }

    auto conn_r = co_await async_connect(io, target, *target_ep_r);
    
    // Send response
    socks5_response resp;
    if (conn_r) {
        resp = make_success_response(target);
    } else {
        resp = make_any_address_response(map_connect_error(conn_r.error()));
    }
    
    auto write_r = co_await send_response(io, client, resp);
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
    co_await when_all(
        relay_one_direction(io, client, target),
        relay_one_direction(io, target, client));

    client.close();
    target.close();
}

} // namespace cnetmod::socks5
