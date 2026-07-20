module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.coap;

#ifdef CNETMOD_HAS_SSL

import :coaps_session;

import std;
import :codec;
import cnetmod.core.address;
import cnetmod.core.buffer;
import cnetmod.core.dtls;
import cnetmod.core.socket;
import cnetmod.coro.task;
import cnetmod.io.io_context;

namespace cnetmod::coap {

coaps_session::coaps_session(io_context& ctx,
                             ssl_context& ssl_ctx,
                             socket& sock,
                             endpoint peer,
                             coaps_session_options options,
                             coaps_response_handler response_handler)
    : ctx_(ctx)
    , ssl_ctx_(ssl_ctx)
    , sock_(sock)
    , peer_(std::move(peer))
    , options_(options)
    , response_handler_(std::move(response_handler))
{}

auto coaps_session::run(std::shared_ptr<coaps_datagram_queue> inbound) -> task<void> {
    dtls_datagram_session session{
        ssl_ctx_,
        ctx_,
        sock_,
        peer_,
        dtls_role::server,
        dtls_datagram_options{.mtu = options_.dtls_mtu, .recv_buffer_size = options_.max_datagram_size},
    };

    session.set_receive_handler([inbound]() -> task<std::expected<std::vector<std::byte>, std::error_code>> {
        auto datagram = co_await inbound->receive();
        if (!datagram) {
            co_return std::unexpected(std::make_error_code(std::errc::connection_reset));
        }
        co_return std::move(*datagram);
    });

    auto hs = co_await session.async_handshake();
    if (!hs) {
        inbound->close();
        co_return;
    }

    std::vector<std::byte> in(options_.max_datagram_size);
    for (;;) {
        auto rd = co_await session.async_read(mutable_buffer{in.data(), in.size()});
        if (!rd || *rd == 0) {
            break;
        }

        auto parsed = parse_message(std::span<const std::byte>{in.data(), *rd});
        if (!parsed || !parsed->is_request()) {
            continue;
        }

        auto inbound_request = make_request_view(*parsed);
        auto resp = response_handler_
            ? co_await response_handler_(inbound_request, peer_)
            : make_response(*parsed, response_code::not_implemented);
        normalize_response(*parsed, resp);

        auto raw = serialize_message(resp);
        if (!raw || raw->size() > options_.max_datagram_size) {
            auto err = make_response(*parsed, response_code::internal_server_error);
            normalize_response(*parsed, err);
            raw = serialize_message(err);
            if (!raw) {
                continue;
            }
        }

        (void)co_await session.async_write(const_buffer{raw->data(), raw->size()});
    }

    inbound->close();
}

auto coaps_session::make_request_view(const message& msg) -> inbound_request {
    return inbound_request{
        .request = msg,
        .path = extract_path(msg),
        .query = extract_query(msg),
    };
}

void coaps_session::normalize_response(const message& req, message& resp) {
    if (resp.message_id == 0) {
        resp.message_id = req.message_id;
    }
    if (resp.token.empty()) {
        resp.token = req.token;
    }
    if (req.type == message_type::confirmable && resp.type == message_type::confirmable) {
        resp.type = message_type::acknowledgement;
    }
}

} // namespace cnetmod::coap

#endif // CNETMOD_HAS_SSL
