include_guard(GLOBAL)

include(${CMAKE_CURRENT_LIST_DIR}/Stdexec.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/JwtCpp.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/NlohmannJson.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/Pugixml.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/OpenSSL.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/BoringSSL.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/Zlib.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/Lz4.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/Leveldb.cmake)
include(${CMAKE_CURRENT_LIST_DIR}/Icu.cmake)

macro(cnetmod_configure_third_party_dependencies)
    # BoringSSL is configured before the system OpenSSL fallback.  Declare the
    # shared provider switch first; otherwise an undefined CNETMOD_ENABLE_SSL
    # makes the bundled submodule silently skip its initial configuration.
    option(CNETMOD_ENABLE_SSL "Enable SSL/TLS and cryptographic authentication" ON)
    cnetmod_configure_stdexec()
    cnetmod_configure_nlohmann_json()
    cnetmod_configure_jwt_cpp()
    cnetmod_configure_pugixml()
    cnetmod_configure_boringssl_quic()
    cnetmod_configure_openssl()
    cnetmod_configure_zlib()
    cnetmod_configure_lz4()
    cnetmod_configure_leveldb()
    cnetmod_configure_icu()
endmacro()

function(cnetmod_link_third_party_dependencies TARGET_NAME)
    cnetmod_link_stdexec(${TARGET_NAME})
    cnetmod_link_nlohmann_json(${TARGET_NAME})
    cnetmod_link_jwt_cpp(${TARGET_NAME})
    cnetmod_link_pugixml(${TARGET_NAME})
    cnetmod_link_openssl(${TARGET_NAME})

    # BoringSSL is the default TLS provider when its bundled submodule is
    # available. It also supplies the crypto API consumed by jwt-cpp.
    if(BoringSSL_FOUND AND DEFINED BoringSSL_LIBRARIES)
        target_link_libraries(${TARGET_NAME} PRIVATE ${BoringSSL_LIBRARIES})
        if(DEFINED BoringSSL_INCLUDE_DIRS)
            # OpenSSL and BoringSSL install identically named <openssl/...>
            # headers.  The BoringSSL headers use direct replacement APIs
            # (for example SSL_CTX_set_options), whereas OpenSSL expands some
            # calls to legacy SSL*_ctrl symbols that BoringSSL does not export.
            # Put the selected provider ahead of incidental package-manager
            # include paths on every platform, especially Homebrew/macOS.
            target_include_directories(${TARGET_NAME} BEFORE PRIVATE
                ${BoringSSL_INCLUDE_DIRS})
        endif()
    endif()

    cnetmod_link_zlib(${TARGET_NAME})
    cnetmod_link_lz4(${TARGET_NAME})
    cnetmod_link_leveldb(${TARGET_NAME})
    cnetmod_link_icu(${TARGET_NAME})
endfunction()
