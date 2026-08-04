#pragma once

// =============================================================================
// BoringSSL QUIC callback support header.
//
// The SSL_QUIC_METHOD callbacks are implemented as static member functions
// of quic_tls_session (see quic_crypto.cppm / quic_crypto.cpp). The session
// pointer is recovered inside each callback via SSL_get_app_data(), which is
// installed in quic_tls_session::register_quic_callbacks(). No separate
// userdata index or extern "C" glue is required.
// =============================================================================

#include <cnetmod/config.hpp>

#ifdef CNETMOD_HAS_SSL
#ifdef CNETMOD_ENABLE_QUIC

#include <openssl/ssl.h>

#endif // CNETMOD_ENABLE_QUIC
#endif // CNETMOD_HAS_SSL
