# QUIC handshake verification tools

if(CNETMOD_HAS_QUIC)
    # HTTP/3 interop test binaries (server + client).
    # testing/tests may already define these targets; avoid duplicates.
    if(NOT TARGET h3_interop_server)
        add_executable(h3_interop_server
            h3_interop_server.cpp)
        target_link_libraries(h3_interop_server PRIVATE cnetmod_core)
        cnetmod_deploy_mimalloc_runtime(h3_interop_server)
        set_property(TARGET h3_interop_server PROPERTY CXX_MODULE_GENERATION_MODE SEPARATE)
        set_target_properties(h3_interop_server PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/)
    endif()

    if(NOT TARGET h3_interop_client)
        add_executable(h3_interop_client
            h3_interop_client.cpp)
        target_link_libraries(h3_interop_client PRIVATE cnetmod_core)
        cnetmod_deploy_mimalloc_runtime(h3_interop_client)
        set_property(TARGET h3_interop_client PROPERTY CXX_MODULE_GENERATION_MODE SEPARATE)
        set_target_properties(h3_interop_client PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/)
    endif()

    add_executable(quic_pmtu_deadline_server quic_pmtu_deadline_server.cpp)
    target_link_libraries(quic_pmtu_deadline_server PRIVATE cnetmod_core)
    cnetmod_deploy_mimalloc_runtime(quic_pmtu_deadline_server)
    set_property(TARGET quic_pmtu_deadline_server PROPERTY CXX_MODULE_GENERATION_MODE SEPARATE)
    set_target_properties(quic_pmtu_deadline_server PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/)

    add_executable(quic_datagram_cc_server quic_datagram_cc_server.cpp)
    target_link_libraries(quic_datagram_cc_server PRIVATE cnetmod_core)
    cnetmod_deploy_mimalloc_runtime(quic_datagram_cc_server)
    set_property(TARGET quic_datagram_cc_server PROPERTY CXX_MODULE_GENERATION_MODE SEPARATE)
    set_target_properties(quic_datagram_cc_server PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/)

    add_executable(quic_pmtu_cc_server quic_pmtu_cc_server.cpp)
    target_link_libraries(quic_pmtu_cc_server PRIVATE cnetmod_core)
    cnetmod_deploy_mimalloc_runtime(quic_pmtu_cc_server)
    set_property(TARGET quic_pmtu_cc_server PROPERTY CXX_MODULE_GENERATION_MODE SEPARATE)
    set_target_properties(quic_pmtu_cc_server PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/)

    add_executable(quic_retire_stream_server quic_retire_stream_server.cpp)
    target_link_libraries(quic_retire_stream_server PRIVATE cnetmod_core)
    cnetmod_deploy_mimalloc_runtime(quic_retire_stream_server)
    set_property(TARGET quic_retire_stream_server PROPERTY CXX_MODULE_GENERATION_MODE SEPARATE)
    set_target_properties(quic_retire_stream_server PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin/)

    # Public http::client -> http3_server regression fixture.  This is kept
    # separate from the low-level v3 interop client so the test exercises the
    # same unified API that applications use.
    if(CNETMOD_ENABLE_HTTP AND NOT TARGET http_client_http3_e2e)
        add_executable(http3_e2e_server
            http_client_e2e_server.cpp)
        target_link_libraries(http3_e2e_server PRIVATE cnetmod_core)
        set_property(TARGET http3_e2e_server PROPERTY CXX_MODULE_GENERATION_MODE SEPARATE)

        add_executable(http_client_http3_e2e
            http_client_e2e.cpp)
        target_link_libraries(http_client_http3_e2e PRIVATE cnetmod_core)
        set_property(TARGET http_client_http3_e2e PROPERTY CXX_MODULE_GENERATION_MODE SEPARATE)
    endif()

    if(CNETMOD_ENABLE_HTTP AND NOT TARGET http3_migration_e2e)
        add_executable(http3_migration_e2e
            http3_migration_e2e.cpp)
        target_link_libraries(http3_migration_e2e PRIVATE cnetmod_core)
        set_property(TARGET http3_migration_e2e PROPERTY CXX_MODULE_GENERATION_MODE SEPARATE)
    endif()
endif()

# Phase 6: Weak network tests (Python-based, requires root/admin for network emulation)
find_package(Python3 COMPONENTS Interpreter QUIET)

if(Python3_Interpreter_FOUND AND CNETMOD_HAS_QUIC)
    foreach(pmtu_deadline_scenario IN ITEMS interval initial)
        add_test(NAME quic_pmtu_deadline_${pmtu_deadline_scenario}
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/quic_pmtu_deadline.py"
                "$<TARGET_FILE:quic_pmtu_deadline_server>" --scenario ${pmtu_deadline_scenario}
                --helper-dir "${CMAKE_CURRENT_SOURCE_DIR}")
        set_tests_properties(quic_pmtu_deadline_${pmtu_deadline_scenario} PROPERTIES
            LABELS "http3;quic;pmtu;timer" TIMEOUT 30 SKIP_RETURN_CODE 77)
    endforeach()
    foreach(datagram_cc_scenario IN ITEMS admission pto)
        add_test(NAME quic_datagram_cc_${datagram_cc_scenario}
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/quic_datagram_cc.py"
                "$<TARGET_FILE:quic_datagram_cc_server>" --scenario ${datagram_cc_scenario})
        set_tests_properties(quic_datagram_cc_${datagram_cc_scenario} PROPERTIES
            LABELS "http3;quic;datagram;recovery" TIMEOUT 45 SKIP_RETURN_CODE 77)
    endforeach()
    # Explicit process-local cryptography AEAD adapter; this is not stock aioquic interop.
    add_test(NAME quic_pmtu_cc_large_response_after_loss
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/quic_pmtu_cc.py"
            "$<TARGET_FILE:quic_pmtu_cc_server>" --large-aead-peer)
    set_tests_properties(quic_pmtu_cc_large_response_after_loss PROPERTIES
        LABELS "http3;quic;pmtu;recovery;test_peer_adapter" TIMEOUT 90 SKIP_RETURN_CODE 77)
    add_test(NAME http3_lifecycle
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/http3_lifecycle.py"
            "$<TARGET_FILE:h3_interop_server>")
    set_tests_properties(http3_lifecycle PROPERTIES
        LABELS "http3;interop;lifecycle" TIMEOUT 60 SKIP_RETURN_CODE 77)
    add_test(NAME http3_cold_listener_cleanup
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/http3_cold_cleanup.py"
            "$<TARGET_FILE:h3_interop_server>")
    set_tests_properties(http3_cold_listener_cleanup PROPERTIES
        LABELS "http3;interop;lifecycle" TIMEOUT 50 SKIP_RETURN_CODE 77)
    foreach(outbound_workers IN ITEMS 1 2)
        add_test(NAME http3_outbound_pto_close_restart_${outbound_workers}
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/http3_outbound_pto.py"
                "$<TARGET_FILE:h3_interop_server>" --workers ${outbound_workers} --restart)
        set_tests_properties(http3_outbound_pto_close_restart_${outbound_workers} PROPERTIES
            LABELS "http3;interop;lifecycle;recovery" TIMEOUT 30 SKIP_RETURN_CODE 77)
    endforeach()
    add_test(NAME quic_retire_stream_credit_wakeup
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/quic_retire_stream.py"
            "$<TARGET_FILE:quic_retire_stream_server>" --mode all)
    set_tests_properties(quic_retire_stream_credit_wakeup PROPERTIES
        LABELS "http3;quic;lifecycle;flow_control" TIMEOUT 30 SKIP_RETURN_CODE 77)
    add_test(NAME quic_retire_stream_notification_lifetime
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/quic_retire_stream.py"
            "$<TARGET_FILE:quic_retire_stream_server>" --scenario notification-lifetime)
    set_tests_properties(quic_retire_stream_notification_lifetime PROPERTIES
        LABELS "http3;quic;lifecycle;memory" TIMEOUT 30 SKIP_RETURN_CODE 77)
    add_test(NAME quic_private_outbound_pto
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/quic_retire_stream.py"
            "$<TARGET_FILE:quic_retire_stream_server>" --scenario private-pto)
    set_tests_properties(quic_private_outbound_pto PROPERTIES
        LABELS "http3;quic;lifecycle;recovery" TIMEOUT 30 SKIP_RETURN_CODE 77)
    add_test(NAME quic_handshake_confirmation
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/quic_handshake_confirmation.py"
            "$<TARGET_FILE:quic_retire_stream_server>")
    set_tests_properties(quic_handshake_confirmation PROPERTIES
        LABELS "http3;quic;lifecycle;recovery" TIMEOUT 30 SKIP_RETURN_CODE 77)
    add_test(NAME h3_webtransport_aioquic_probe_unit
        COMMAND "${Python3_EXECUTABLE}" -m unittest discover
            -s "${CMAKE_CURRENT_SOURCE_DIR}" -p test_webtransport_aioquic_probe.py)
    set_tests_properties(h3_webtransport_aioquic_probe_unit PROPERTIES
        LABELS "http3;interop;webtransport;unit"
        TIMEOUT 30)
    set(CNETMOD_WTRANSPORT_PROBE "" CACHE FILEPATH
        "Rust wtransport_probe executable used by the HTTP/3 acceptance gate")
    set(H3_ACCEPTANCE_WTRANSPORT_ARGS)
    if(CNETMOD_WTRANSPORT_PROBE)
        if(EXISTS "${CNETMOD_WTRANSPORT_PROBE}")
            list(APPEND H3_ACCEPTANCE_WTRANSPORT_ARGS
                "--wtransport-probe" "${CNETMOD_WTRANSPORT_PROBE}")
        else()
            message(WARNING
                "CNETMOD_WTRANSPORT_PROBE does not exist; the optional Rust WebTransport probe is disabled (the required aioquic WebTransport gate remains enabled)")
        endif()
    endif()
    add_test(NAME h3_interop_release_gate
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/h3_acceptance.py"
                "--server" "$<TARGET_FILE:h3_interop_server>"
                "--client" "$<TARGET_FILE:h3_interop_client>"
                "--port" "4433" ${H3_ACCEPTANCE_WTRANSPORT_ARGS}
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
    set_tests_properties(h3_interop_release_gate PROPERTIES
        TIMEOUT 120
        LABELS "http3;interop;release_gate"
        SKIP_RETURN_CODE 77)

    if(WIN32)
        add_test(NAME h3_interop_multi_worker_release_gate
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/h3_acceptance.py"
                    "--server" "$<TARGET_FILE:h3_interop_server>"
                    "--client" "$<TARGET_FILE:h3_interop_client>"
                    "--server-workers" "2" "--port" "4434" ${H3_ACCEPTANCE_WTRANSPORT_ARGS}
            WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
        set_tests_properties(h3_interop_multi_worker_release_gate PROPERTIES
            TIMEOUT 120
            LABELS "http3;interop;release_gate;multi_worker"
            SKIP_RETURN_CODE 77)
    endif()

    if(TARGET http3_e2e_server AND TARGET http_client_http3_e2e)
        add_test(NAME http3_early_data_recovery
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/http_client_e2e.py" "--early-data-loss"
                "$<TARGET_FILE:http3_e2e_server>" "$<TARGET_FILE:http_client_http3_e2e>")
        set_tests_properties(http3_early_data_recovery PROPERTIES
            TIMEOUT 90 LABELS "http3;quic;recovery" SKIP_RETURN_CODE 77)
        add_test(NAME http3_streaming_cancellation
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/http3_streaming_cancellation.py"
                "$<TARGET_FILE:http3_e2e_server>")
        set_tests_properties(http3_streaming_cancellation PROPERTIES
            TIMEOUT 20 LABELS "http3;quic;lifecycle" SKIP_RETURN_CODE 77)
        add_test(NAME http3_idle_connection_cleanup
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/http3_idle_cleanup.py"
                "$<TARGET_FILE:http3_e2e_server>")
        set_tests_properties(http3_idle_connection_cleanup PROPERTIES
            TIMEOUT 65 LABELS "http3;quic;lifecycle" SKIP_RETURN_CODE 77)
        add_test(NAME http_client_http3_end_to_end
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/http_client_e2e.py"
                "$<TARGET_FILE:http3_e2e_server>"
                "$<TARGET_FILE:http_client_http3_e2e>")
        set_tests_properties(http_client_http3_end_to_end PROPERTIES
            TIMEOUT 90
            LABELS "http3;client;end_to_end"
            SKIP_RETURN_CODE 77)

        # Exercise RFC 9204 blocked-header recovery as it occurs on the wire:
        # the request HEADERS stream can be scheduled before the peer has
        # consumed its QPACK encoder-stream inserts.
        add_test(NAME http_client_http3_dynamic_qpack_end_to_end
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/http_client_e2e.py"
                "--dynamic-qpack"
                "$<TARGET_FILE:http3_e2e_server>"
                "$<TARGET_FILE:http_client_http3_e2e>")
        set_tests_properties(http_client_http3_dynamic_qpack_end_to_end PROPERTIES
            TIMEOUT 90
            LABELS "http3;client;qpack;end_to_end"
            SKIP_RETURN_CODE 77)

        # Both endpoints opt in, exchange path-specific CIDs, validate Path
        # ID 1 and complete an HTTP/3 request after the new path is live.
        add_test(NAME http3_multipath_end_to_end
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/http_client_e2e.py"
                "--multipath"
                "$<TARGET_FILE:http3_e2e_server>"
                "$<TARGET_FILE:http_client_http3_e2e>")
        set_tests_properties(http3_multipath_end_to_end PROPERTIES
            TIMEOUT 90
            LABELS "http3;quic;multipath;end_to_end"
            SKIP_RETURN_CODE 77)

        add_test(NAME http3_multipath_path_mtu_end_to_end
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/http_client_e2e.py"
                "--multipath-pmtu"
                "$<TARGET_FILE:http3_e2e_server>"
                "$<TARGET_FILE:http_client_http3_e2e>")
        set_tests_properties(http3_multipath_path_mtu_end_to_end PROPERTIES
            TIMEOUT 90
            LABELS "http3;quic;multipath;pmtu;end_to_end"
            SKIP_RETURN_CODE 77)

        # A real PMTU black hole drops every datagram above the RFC 9000
        # baseline. The Path 1 probe must not raise its ceiling and the
        # separately bound Path 1 socket must still carry normal HTTP/3 data.
        add_test(NAME http3_multipath_path_mtu_blackhole_end_to_end
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/http_client_e2e.py"
                "--multipath-pmtu-blackhole"
                "--pmtu-blackhole-proxy"
                "${CMAKE_CURRENT_SOURCE_DIR}/pmtu_blackhole_proxy.py"
                "$<TARGET_FILE:http3_e2e_server>"
                "$<TARGET_FILE:http_client_http3_e2e>")
        set_tests_properties(http3_multipath_path_mtu_blackhole_end_to_end PROPERTIES
            TIMEOUT 90
            LABELS "http3;quic;multipath;pmtu;blackhole;end_to_end"
            SKIP_RETURN_CODE 77)

        # close() must cancel an unanswered probe on an independently bound
        # local Path 1 socket and join its receiver before releasing QUIC.
        add_test(NAME http3_multipath_local_path_close_end_to_end
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/http_client_e2e.py"
                "--multipath-close"
                "$<TARGET_FILE:http3_e2e_server>"
                "$<TARGET_FILE:http_client_http3_e2e>")
        set_tests_properties(http3_multipath_local_path_close_end_to_end PROPERTIES
            TIMEOUT 90
            LABELS "http3;quic;multipath;close;end_to_end"
            SKIP_RETURN_CODE 77)

        # close() during DNS/handshake must prevent that attempt from
        # publishing a new HTTP/3 session after shutdown has returned.
        add_test(NAME http3_connect_close_end_to_end
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/http_client_e2e.py"
                "--connect-close"
                "$<TARGET_FILE:http3_e2e_server>"
                "$<TARGET_FILE:http_client_http3_e2e>")
        set_tests_properties(http3_connect_close_end_to_end PROPERTIES
            TIMEOUT 90
            LABELS "http3;quic;client;close;connect;end_to_end"
            SKIP_RETURN_CODE 77)

        # A UDP send completion is not path validation. The client must wait
        # for PATH_RESPONSE, time out cleanly when it never arrives, and keep
        # the already validated Path ID 0 usable.
        add_test(NAME http3_multipath_path_validation_timeout_end_to_end
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/http_client_e2e.py"
                "--multipath-timeout"
                "$<TARGET_FILE:http3_e2e_server>"
                "$<TARGET_FILE:http_client_http3_e2e>")
        set_tests_properties(http3_multipath_path_validation_timeout_end_to_end PROPERTIES
            TIMEOUT 90
            LABELS "http3;quic;multipath;timeout;end_to_end"
            SKIP_RETURN_CODE 77)

    endif()

    if(TARGET http3_migration_e2e AND TARGET h3_interop_server)
        add_test(NAME http3_connection_migration
            COMMAND "${Python3_EXECUTABLE}"
                "${CMAKE_CURRENT_SOURCE_DIR}/migration_e2e.py"
                "$<TARGET_FILE:h3_interop_server>"
                "$<TARGET_FILE:http3_migration_e2e>"
                "${CMAKE_CURRENT_SOURCE_DIR}/migration_udp_proxy.py")
        set_tests_properties(http3_connection_migration PROPERTIES
            TIMEOUT 120
            LABELS "http3;quic;migration"
            SKIP_RETURN_CODE 77)
    endif()
endif()

# The aggregate cnetmod_build_all target is the CI build contract.  Keep every
# executable referenced by an HTTP/3 CTest command in that aggregate; otherwise
# a clean build can register a test whose fixture binary was never produced.
if(CNETMOD_HAS_QUIC)
    foreach(http3_test_target IN ITEMS
            h3_interop_server
            quic_retire_stream_server
            quic_pmtu_cc_server
            quic_pmtu_deadline_server
            quic_datagram_cc_server
            h3_interop_client
            http3_e2e_server
            http_client_http3_e2e
            http3_migration_e2e)
        if(TARGET ${http3_test_target})
            list(APPEND CNETMOD_TEST_TARGETS ${http3_test_target})
        endif()
    endforeach()
endif()

set(CNETMOD_TEST_TARGETS ${CNETMOD_TEST_TARGETS} PARENT_SCOPE)
