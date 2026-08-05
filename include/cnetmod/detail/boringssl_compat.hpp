#pragma once

#include <cstdint>

// BoringSSL and OpenSSL deliberately share <openssl/...> include names.  If a
// package-manager OpenSSL header leaks into a BoringSSL build, OpenSSL's legacy
// function-like macros turn these calls into SSL*_ctrl / SSL_get1_* symbols.
// BoringSSL does not export those symbols.  Select BoringSSL's direct API
// explicitly so an accidental header-path leak cannot produce a link-time
// ABI mismatch.
#if defined(CNETMOD_USING_BORINGSSL)
    #ifdef SSL_CTX_set_min_proto_version
        #undef SSL_CTX_set_min_proto_version
    #endif
    #ifdef SSL_CTX_set_max_proto_version
        #undef SSL_CTX_set_max_proto_version
    #endif
    #ifdef SSL_CTX_set_options
        #undef SSL_CTX_set_options
    #endif
    #ifdef SSL_CTX_clear_options
        #undef SSL_CTX_clear_options
    #endif
    #ifdef SSL_set_options
        #undef SSL_set_options
    #endif
    #ifdef SSL_set_tlsext_host_name
        #undef SSL_set_tlsext_host_name
    #endif
    #ifdef SSL_get_peer_certificate
        #undef SSL_get_peer_certificate
    #endif

extern "C" {
int SSL_CTX_set_min_proto_version(SSL_CTX* context, std::uint16_t version);
int SSL_CTX_set_max_proto_version(SSL_CTX* context, std::uint16_t version);
std::uint32_t SSL_CTX_set_options(SSL_CTX* context, std::uint32_t options);
std::uint32_t SSL_CTX_clear_options(SSL_CTX* context, std::uint32_t options);
std::uint32_t SSL_set_options(SSL* ssl, std::uint32_t options);
int SSL_set_tlsext_host_name(SSL* ssl, const char* name);
X509* SSL_get_peer_certificate(const SSL* ssl);
}
#endif
