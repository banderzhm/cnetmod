/// cnetmod.protocol.coap:coaps_session - one CoAPS peer session.

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.coap:coaps_session;

#ifdef CNETMOD_HAS_SSL

import std;
import :types;
import :server;
import cnetmod.core.address;
import cnetmod.core.socket;
import cnetmod.core.ssl;
import cnetmod.coro.channel;
import cnetmod.coro.task;
import cnetmod.io.io_context;

namespace cnetmod::coap {

export struct coaps_session_options {
    std::size_t max_datagram_size = default_max_datagram_size;
    std::size_t dtls_mtu = 1400;
};

export using coaps_datagram_queue = channel<std::vector<std::byte>>;
export using coaps_response_handler =
    std::function<task<message>(const inbound_request&, const endpoint&)>;

export class coaps_session {
public:
    coaps_session(io_context& ctx,
                  ssl_context& ssl_ctx,
                  socket& sock,
                  endpoint peer,
                  coaps_session_options options,
                  coaps_response_handler response_handler);

    coaps_session(const coaps_session&) = delete;
    auto operator=(const coaps_session&) -> coaps_session& = delete;

    auto run(std::shared_ptr<coaps_datagram_queue> inbound) -> task<void>;

private:
    static auto make_request_view(const message& msg) -> inbound_request;
    static void normalize_response(const message& req, message& resp);

    io_context& ctx_;
    ssl_context& ssl_ctx_;
    socket& sock_;
    endpoint peer_;
    coaps_session_options options_;
    coaps_response_handler response_handler_;
};

} // namespace cnetmod::coap

#endif // CNETMOD_HAS_SSL
