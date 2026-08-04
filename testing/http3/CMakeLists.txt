# QUIC handshake verification tools

if(CNETMOD_HAS_QUIC)
    # Phase 1: Basic echo server for handshake verification
    add_executable(quic_echo_server
        quic_echo_server.cpp
        quic_echo_server_main.cpp)
    target_link_libraries(quic_echo_server PRIVATE cnetmod_core)

    set_property(TARGET quic_echo_server PROPERTY CXX_MODULE_GENERATION_MODE SEPARATE)
    set_target_properties(quic_echo_server PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/)

    # Phase 2: Minimal QUIC echo server for comprehensive handshake testing
    add_executable(quic_echo_server_minimal
        quic_echo_server_minimal.cpp
        quic_echo_server_minimal_main.cpp)
    target_link_libraries(quic_echo_server_minimal PRIVATE cnetmod_core)

    set_property(TARGET quic_echo_server_minimal PROPERTY CXX_MODULE_GENERATION_MODE SEPARATE)
    set_target_properties(quic_echo_server_minimal PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/)

    # Phase 5: HTTP/3 interop test binaries (server + client).
    # testing/tests may already define these targets; avoid duplicates.
    if(NOT TARGET h3_interop_server)
        add_executable(h3_interop_server
            h3_interop_server.cpp)
        target_link_libraries(h3_interop_server PRIVATE cnetmod_core)
        set_property(TARGET h3_interop_server PROPERTY CXX_MODULE_GENERATION_MODE SEPARATE)
        set_target_properties(h3_interop_server PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/)
    endif()

    if(NOT TARGET h3_interop_client)
        add_executable(h3_interop_client
            h3_interop_client.cpp)
        target_link_libraries(h3_interop_client PRIVATE cnetmod_core)
        set_property(TARGET h3_interop_client PROPERTY CXX_MODULE_GENERATION_MODE SEPARATE)
        set_target_properties(h3_interop_client PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/)
    endif()
endif()

# Phase 6: Weak network tests (Python-based, requires root/admin for network emulation)
find_package(Python3 COMPONENTS Interpreter QUIET)

if(Python3_Interpreter_FOUND AND CNETMOD_HAS_QUIC)
    add_test(NAME h3_interop_release_gate
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/h3_acceptance.py"
                "--server" "$<TARGET_FILE:h3_interop_server>"
                "--client" "$<TARGET_FILE:h3_interop_client>"
                "--port" "4433"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    set_tests_properties(h3_interop_release_gate PROPERTIES
        TIMEOUT 120
        LABELS "http3;interop;release_gate"
        SKIP_RETURN_CODE 77)
endif()
