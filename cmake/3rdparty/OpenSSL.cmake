include_guard(GLOBAL)

macro(cnetmod_configure_openssl)
    option(CNETMOD_ENABLE_SSL "Enable SSL/TLS and cryptographic authentication" ON)
    set(CNETMOD_HAS_SSL OFF)
    # BoringSSL and OpenSSL export the same C API symbols.  Linking both is
    # invalid: the dynamic loader may resolve TLS_server_method from OpenSSL
    # while SSL_CTX_new comes from BoringSSL, which corrupts SSL_CTX creation.
    # The QUIC option is a cache variable and is therefore available before
    # BoringSSL.cmake declares it with option().
    if(CNETMOD_ENABLE_SSL AND NOT CNETMOD_ENABLE_BORINGSSL_QUIC)
        find_package(OpenSSL QUIET)
        if(OpenSSL_FOUND)
            set(CNETMOD_HAS_SSL ON)
        else()
            message(STATUS "OpenSSL not found: TLS and OpenSSL-backed authentication are disabled")
        endif()
    elseif(CNETMOD_ENABLE_SSL AND CNETMOD_ENABLE_BORINGSSL_QUIC)
        # BoringSSL is configured immediately afterwards.  Do not discover or
        # link system OpenSSL in this configuration.
        message(STATUS "Using BoringSSL with QUIC - skipping system OpenSSL")
        set(CNETMOD_HAS_SSL OFF)
    endif()
endmacro()

function(cnetmod_link_openssl TARGET_NAME)
    # A stale CMake cache may still contain CNETMOD_HAS_SSL=ON after switching
    # from OpenSSL to BoringSSL.  The imported targets are the authoritative
    # indication that this target may link the system provider.
    if(CNETMOD_HAS_SSL AND TARGET OpenSSL::SSL AND TARGET OpenSSL::Crypto)
        target_link_libraries(${TARGET_NAME} PUBLIC OpenSSL::SSL OpenSSL::Crypto)
    endif()
endfunction()
