module;

#include <cnetmod/config.hpp>

module cnetmod.protocol.coap;

#ifdef CNETMOD_HAS_SSL

import :coaps_security;

import std;

namespace cnetmod::coap {

auto coaps_security_config::insecure_for_testing() -> coaps_security_config {
    coaps_security_config cfg;
    cfg.verify_peer = coaps_peer_verification::none;
    cfg.identity = coaps_identity_policy::none;
    return cfg;
}

auto coaps_security_config::verified_peer(std::string peer_name) -> coaps_security_config {
    coaps_security_config cfg;
    cfg.verify_peer = coaps_peer_verification::required;
    cfg.identity = peer_name.empty()
        ? coaps_identity_policy::remote_address
        : coaps_identity_policy::configured_name;
    cfg.peer_name = std::move(peer_name);
    cfg.use_default_ca = true;
    return cfg;
}

auto configure_coaps_context(ssl_context& ctx,
                             const coaps_security_config& cfg)
    -> std::expected<void, std::error_code>
{
    if (!cfg.ca_file.empty()) {
        if (auto r = ctx.load_ca_file(cfg.ca_file); !r) {
            return std::unexpected(r.error());
        }
    }
    if (cfg.use_default_ca) {
        if (auto r = ctx.set_default_ca(); !r) {
            return std::unexpected(r.error());
        }
    }

    switch (cfg.verify_peer) {
    case coaps_peer_verification::context_default:
        break;
    case coaps_peer_verification::none:
        ctx.set_verify_peer(false);
        break;
    case coaps_peer_verification::required:
        ctx.set_verify_peer(true);
        break;
    }

    return {};
}

void configure_coaps_session_identity(dtls_datagram_session& session,
                                      const endpoint& remote,
                                      const coaps_security_config& cfg)
{
    if (cfg.verify_peer == coaps_peer_verification::none) {
        return;
    }

    switch (cfg.identity) {
    case coaps_identity_policy::none:
        return;
    case coaps_identity_policy::configured_name:
        if (!cfg.peer_name.empty()) {
            session.set_hostname(cfg.peer_name);
        }
        return;
    case coaps_identity_policy::remote_address:
        session.set_hostname(remote.address().to_string());
        return;
    }
}

auto classify_coaps_security_error(const std::error_code& ec) noexcept
    -> coaps_security_failure
{
    if (!ec) {
        return coaps_security_failure::none;
    }
    if (ec == std::make_error_code(std::errc::timed_out) ||
        ec == std::make_error_code(std::errc::connection_reset) ||
        ec == std::make_error_code(std::errc::connection_aborted) ||
        ec == std::make_error_code(std::errc::network_unreachable) ||
        ec == std::make_error_code(std::errc::host_unreachable)) {
        return coaps_security_failure::transport;
    }
    if (ec.category().name() == std::string_view{"openssl"}) {
        return coaps_security_failure::handshake;
    }
    return coaps_security_failure::context_configuration;
}

} // namespace cnetmod::coap

#endif // CNETMOD_HAS_SSL
