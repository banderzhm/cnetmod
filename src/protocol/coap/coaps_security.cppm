/// cnetmod.protocol.coap:coaps_security - CoAPS TLS/DTLS security policy.

module;

#include <cnetmod/config.hpp>

export module cnetmod.protocol.coap:coaps_security;

#ifdef CNETMOD_HAS_SSL

import std;
import cnetmod.core.address;
import cnetmod.core.dtls;
import cnetmod.core.ssl;

namespace cnetmod::coap {

export enum class coaps_peer_verification {
    context_default,
    none,
    required,
};

export enum class coaps_identity_policy {
    none,
    configured_name,
    remote_address,
};

export enum class coaps_security_failure {
    none,
    context_configuration,
    certificate_verification,
    identity_verification,
    handshake,
    transport,
};

export struct coaps_security_config {
    coaps_peer_verification verify_peer = coaps_peer_verification::context_default;
    coaps_identity_policy identity = coaps_identity_policy::remote_address;
    std::string peer_name;
    std::string ca_file;
    bool use_default_ca = false;

    [[nodiscard]] static auto insecure_for_testing() -> coaps_security_config;
    [[nodiscard]] static auto verified_peer(std::string peer_name = {}) -> coaps_security_config;
};

export [[nodiscard]] auto configure_coaps_context(
    ssl_context& ctx,
    const coaps_security_config& cfg) -> std::expected<void, std::error_code>;

export void configure_coaps_session_identity(
    dtls_datagram_session& session,
    const endpoint& remote,
    const coaps_security_config& cfg);

export [[nodiscard]] auto classify_coaps_security_error(
    const std::error_code& ec) noexcept -> coaps_security_failure;

} // namespace cnetmod::coap

#endif // CNETMOD_HAS_SSL
