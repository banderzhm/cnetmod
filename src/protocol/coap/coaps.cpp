module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.coap;

#ifdef CNETMOD_HAS_SSL

import :coaps;

import std;
import :codec;
import :coaps_security;
import :facade;
import cnetmod.core.address;
import cnetmod.core.buffer;
import cnetmod.core.dtls;
import cnetmod.coro.task;
import cnetmod.executor.async_op;
import cnetmod.io.io_context;

namespace cnetmod::coap {

secure_client::secure_client(io_context& ctx, ssl_context& ssl_ctx,
                             secure_client_config cfg)
    : ctx_(ctx)
    , ssl_ctx_(ssl_ctx)
    , cfg_(cfg)
    , rng_(std::random_device{}())
{}

auto secure_client::request(const endpoint& remote, message req)
    -> task<std::expected<message, std::error_code>>
{
    auto opened = socket::create(remote.address().is_v4()
        ? address_family::ipv4 : address_family::ipv6, socket_type::datagram);
    if (!opened) {
        co_return std::unexpected(opened.error());
    }
    auto request_sock = std::move(*opened);
    (void)request_sock.set_non_blocking(true);

    if (req.code == 0 || !req.is_request()) {
        co_return std::unexpected(std::make_error_code(std::errc::invalid_argument));
    }
    if (req.message_id == 0) {
        req.message_id = next_message_id();
    }
    if (req.token.empty()) {
        req.token = next_token();
    }

    auto raw = serialize_message(req);
    if (!raw) {
        co_return std::unexpected(raw.error());
    }
    if (raw->size() > cfg_.coap.max_datagram_size) {
        co_return std::unexpected(std::make_error_code(std::errc::message_size));
    }
    if (auto security = configure_coaps_context(ssl_ctx_, cfg_.security); !security) {
        co_return std::unexpected(security.error());
    }

    dtls_datagram_session session{
        ssl_ctx_,
        ctx_,
        request_sock,
        remote,
        dtls_role::client,
        dtls_datagram_options{.mtu = cfg_.dtls_mtu, .recv_buffer_size = cfg_.coap.max_datagram_size},
    };
    configure_coaps_session_identity(session, remote, cfg_.security);

    auto hs = co_await session.async_handshake();
    if (!hs) {
        co_return std::unexpected(hs.error());
    }

    auto sent = co_await session.async_write(const_buffer{raw->data(), raw->size()});
    if (!sent) {
        co_return std::unexpected(sent.error());
    }

    std::vector<std::byte> in(cfg_.coap.max_datagram_size);
    auto rd = co_await session.async_read(mutable_buffer{in.data(), in.size()});
    if (!rd) {
        co_return std::unexpected(rd.error());
    }

    auto parsed = parse_message(std::span<const std::byte>{in.data(), *rd});
    if (!parsed) {
        co_return std::unexpected(parsed.error());
    }
    if (parsed->token != req.token || !parsed->is_response()) {
        co_return std::unexpected(std::make_error_code(std::errc::protocol_error));
    }
    request_sock.close();
    co_return std::move(*parsed);
}

auto secure_client::get(const endpoint& remote, std::string path, std::string query)
    -> task<std::expected<message, std::error_code>>
{
    co_return co_await request(remote, make_request(request_options{
        .method_code = method::get,
        .path = std::move(path),
        .query = std::move(query),
    }));
}

auto secure_client::post(const endpoint& remote, std::string path, std::vector<std::byte> payload,
                         content_format format)
    -> task<std::expected<message, std::error_code>>
{
    co_return co_await request(remote, make_request(request_options{
        .method_code = method::post,
        .path = std::move(path),
        .content_type = format,
        .payload = std::move(payload),
    }));
}

void secure_client::close() noexcept {
    sock_.close();
}

auto secure_client::next_message_id() -> std::uint16_t {
    return message_id_.fetch_add(1, std::memory_order_relaxed);
}

auto secure_client::next_token() -> std::vector<std::byte> {
    std::vector<std::byte> token(8);
    for (auto& b : token) {
        b = static_cast<std::byte>(std::uniform_int_distribution<int>{0, 255}(rng_));
    }
    return token;
}

secure_server::secure_server(io_context& ctx, ssl_context& ssl_ctx,
                             secure_server_config cfg)
    : ctx_(ctx)
    , ssl_ctx_(ssl_ctx)
    , cfg_(cfg)
{
    sessions_ = std::make_unique<coaps_session_manager>(
        ctx_,
        ssl_ctx_,
        sock_,
        coaps_session_manager_options{
            .session = coaps_session_options{
                .max_datagram_size = cfg_.coap.max_datagram_size,
                .dtls_mtu = cfg_.dtls_mtu,
            },
            .max_sessions = cfg_.max_sessions,
            .queue_capacity = cfg_.session_queue_capacity,
            .idle_timeout = cfg_.session_idle_timeout,
        });
    refresh_session_handler();
}

secure_server::~secure_server() = default;

auto secure_server::listen(std::string_view host, std::uint16_t port, socket_options opts)
    -> std::expected<void, std::error_code>
{
    auto addr = ip_address::from_string(host);
    if (!addr) {
        return std::unexpected(addr.error());
    }

    auto sock_result = socket::create(
        addr->is_v4() ? address_family::ipv4 : address_family::ipv6,
        socket_type::datagram);
    if (!sock_result) {
        return std::unexpected(sock_result.error());
    }

    sock_ = std::move(*sock_result);
    opts.reuse_address = true;
    opts.non_blocking = true;
    if (auto applied = sock_.apply_options(opts); !applied) {
        return std::unexpected(applied.error());
    }
    if (auto security = configure_coaps_context(ssl_ctx_, cfg_.client_security); !security) {
        return std::unexpected(security.error());
    }
    return sock_.bind(endpoint{*addr, port});
}

void secure_server::set_handler(request_handler handler) {
    handler_ = std::move(handler);
    refresh_session_handler();
}

void secure_server::set_etag_provider(etag_provider provider) {
    etag_provider_ = std::move(provider);
    refresh_session_handler();
}

void secure_server::route(method m, std::string path, request_handler handler) {
    router_.add(m, std::move(path), std::move(handler));
    handler_ = [this](const inbound_request& req, const endpoint& peer) {
        return router_.dispatch(req, peer);
    };
    refresh_session_handler();
}

auto secure_server::run() -> task<void> {
    running_ = true;
    std::vector<std::byte> first(cfg_.coap.max_datagram_size);

    while (running_ && sock_.is_open()) {
        endpoint peer;
        auto n = co_await async_recvfrom(ctx_, sock_,
            mutable_buffer{first.data(), first.size()}, peer);
        if (!n || *n == 0) {
            continue;
        }

        (void)sessions_->dispatch(
            peer,
            std::span<const std::byte>{first.data(), *n});
    }
}

auto secure_server::dispatch_request(const inbound_request& req, const endpoint& peer)
    -> task<message>
{
    if (auto precondition = check_preconditions(req)) {
        co_return std::move(*precondition);
    }

    if (handler_) {
        co_return co_await handler_(req, peer);
    }
    co_return make_response(req.request, response_code::not_implemented);
}

void secure_server::refresh_session_handler() {
    if (!sessions_) {
        return;
    }
    sessions_->set_response_handler([this](const inbound_request& req,
                                           const endpoint& peer) -> task<message> {
        co_return co_await dispatch_request(req, peer);
    });
}

void secure_server::stop() noexcept {
    running_ = false;
    sock_.close();
    if (sessions_) {
        sessions_->stop();
    }
}

auto secure_server::check_preconditions(const inbound_request& req) const -> std::optional<message> {
    if (!etag_provider_) {
        return std::nullopt;
    }

    const auto current = etag_provider_(req.path);
    const auto if_match = req.request.find_options(option_number::if_match);
    if (!if_match.empty()) {
        const bool matched = current.has_value() && std::ranges::any_of(if_match, [&](const option& opt) {
            return opt.value.empty() || opt.value == *current;
        });
        if (!matched) {
            return make_response(req.request, response_code::precondition_failed);
        }
    }

    const auto if_none_match = req.request.find_options(option_number::if_none_match);
    if (!if_none_match.empty() && current.has_value()) {
        return make_response(req.request, response_code::precondition_failed);
    }

    return std::nullopt;
}

} // namespace cnetmod::coap

#endif // CNETMOD_HAS_SSL
