# cnetmod test configuration
# Enable with -DCNETMOD_BUILD_TESTS=ON

enable_testing()

# Test source files — each file produces an independent executable
set(TEST_SOURCES
    test_instrumentation.cpp
    test_metric_aggregation.cpp
    test_flat_map.cpp
    test_charconv.cpp
    test_converter.cpp
    test_concurrent_containers.cpp
    test_application_result.cpp
    test_json_template.cpp
    test_json_writer.cpp
    test_application.cpp
    test_application_integrations.cpp
    test_application_composition.cpp
    test_deadline.cpp
    test_task_group.cpp
    test_task.cpp
    test_stream_write.cpp
    test_lazy.cpp
    test_database_datetime.cpp
    test_configuration_properties.cpp
    test_channel.cpp
    test_mutex.cpp
    test_circuit_breaker.cpp
    test_retry.cpp
    test_semaphore.cpp
    test_wait_group.cpp
    test_shared_mutex.cpp
    test_raft.cpp
    test_log.cpp
    test_http_types.cpp
    test_http_tracing.cpp
    test_http_observability.cpp
    test_http_disabled_overhead.cpp
    test_otlp_exporter.cpp
    test_c_api.cpp
    test_http_request.cpp
    test_http_response.cpp
    test_http_compress.cpp
    test_http_parser.cpp
    test_http_router.cpp
    test_http_reverse_proxy.cpp
    test_http_sse.cpp
    test_http_swagger.cpp
    test_http_utils.cpp
    http_client_test.cpp
    test_http_client_pool.cpp
    test_http_ip_filter.cpp
    test_http_jwt_auth.cpp
    test_http_rate_limiter.cpp
    test_http_authorization.cpp
    test_http2_hpack.cpp
    test_http2_stream.cpp
    test_dns.cpp
    test_udp_batch.cpp
    test_coap.cpp
    test_grpc.cpp
    test_grpc_proto.cpp
    test_mqtt.cpp
    test_metrics.cpp
    test_tfb_compliance.cpp
    test_socks5.cpp
    test_mail.cpp
    test_openai.cpp
    test_redis.cpp
    test_redis_template.cpp
    test_mongodb_wire.cpp
    test_mongodb_lifecycle.cpp
    test_mongodb_topology.cpp
    test_mongodb_auth.cpp
    test_mongodb_discovery.cpp
    test_mongodb_cloud_auth.cpp
    test_mongodb_integration.cpp
    test_orm_json_result_map.cpp
    test_orm_sharding.cpp
    test_orm_sharding_sessions.cpp
    test_orm_sharding_gateway.cpp
    test_orm_sharding_scatter.cpp
    test_orm_sharding_transactions.cpp

    # QUIC/HTTP3 tests
    test_quic_varint.cpp
    test_quic_frame.cpp
    test_quic_packet.cpp
    test_quic_in_place_crypto.cpp
    test_quic_stream.cpp
    test_quic_loss_detection.cpp
    test_quic_congestion.cpp
    test_quic_early_data.cpp
    test_http3_qpack.cpp
    test_http3_frame.cpp
)

if(CNETMOD_USING_MIMALLOC)
    list(APPEND TEST_SOURCES test_mimalloc_runtime.cpp)
endif()

if(CNETMOD_ENABLE_SSL)
    list(APPEND TEST_SOURCES test_security_token.cpp)
endif()

if(WIN32)
    list(APPEND TEST_SOURCES test_udp_sendto_on_cancel.cpp)
endif()

if(CNETMOD_ENABLE_WEBSOCKET)
    list(APPEND TEST_SOURCES test_websocket_frame.cpp)
endif()

# Filter out QUIC/HTTP3 tests if QUIC not available
if(NOT CNETMOD_HAS_QUIC)
    list(FILTER TEST_SOURCES EXCLUDE REGEX "^(test_quic|test_http3)")
endif()

if(NOT CNETMOD_ENABLE_RAFT)
    list(REMOVE_ITEM TEST_SOURCES test_raft.cpp)
endif()
if(NOT CNETMOD_ENABLE_HTTP)
    list(FILTER TEST_SOURCES EXCLUDE REGEX
        "^(test_http|test_tfb|test_metrics)")
    list(REMOVE_ITEM TEST_SOURCES test_application.cpp
        test_application_integrations.cpp test_application_composition.cpp
        test_configuration_properties.cpp test_json_template.cpp
        test_otlp_exporter.cpp http_client_test.cpp)
endif()
if(NOT CNETMOD_ENABLE_DNS)
    list(REMOVE_ITEM TEST_SOURCES test_dns.cpp)
endif()
if(NOT CNETMOD_ENABLE_COAP)
    list(REMOVE_ITEM TEST_SOURCES test_coap.cpp)
endif()
if(NOT CNETMOD_ENABLE_GRPC)
    list(FILTER TEST_SOURCES EXCLUDE REGEX "^test_grpc")
endif()
if(NOT CNETMOD_ENABLE_MQTT)
    list(REMOVE_ITEM TEST_SOURCES test_mqtt.cpp)
endif()
if(NOT CNETMOD_ENABLE_SOCKS5)
    list(REMOVE_ITEM TEST_SOURCES test_socks5.cpp)
endif()
if(NOT CNETMOD_ENABLE_MAIL)
    list(REMOVE_ITEM TEST_SOURCES test_mail.cpp)
endif()
if(NOT CNETMOD_ENABLE_OPENAI)
    list(REMOVE_ITEM TEST_SOURCES test_openai.cpp)
endif()
if(NOT CNETMOD_ENABLE_REDIS)
    list(REMOVE_ITEM TEST_SOURCES test_redis.cpp test_redis_template.cpp)
endif()
if(NOT CNETMOD_ENABLE_MONGODB)
    list(REMOVE_ITEM TEST_SOURCES test_mongodb_wire.cpp test_mongodb_lifecycle.cpp test_mongodb_topology.cpp test_mongodb_auth.cpp test_mongodb_discovery.cpp test_mongodb_cloud_auth.cpp test_mongodb_integration.cpp)
endif()
if(NOT TARGET cnetmod_c OR NOT CNETMOD_ENABLE_HTTP)
    list(REMOVE_ITEM TEST_SOURCES test_c_api.cpp)
endif()

set(CNETMOD_TEST_TARGETS)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND TEST_SOURCES test_dns_cancellation.cpp)
endif()
if(CNETMOD_ENABLE_MYSQL AND CNETMOD_ENABLE_HTTP)
    list(APPEND TEST_SOURCES test_application_mysql_live.cpp)
endif()
if(CNETMOD_ENABLE_MYSQL AND CNETMOD_ENABLE_ORM)
    list(APPEND TEST_SOURCES test_application_mysql_sharding_live.cpp)
    list(APPEND TEST_SOURCES test_orm_tenant_live.cpp)
endif()
if(CNETMOD_ENABLE_MYSQL)
    list(APPEND TEST_SOURCES test_mysql_pool.cpp)
endif()
if(CNETMOD_ENABLE_CLICKHOUSE)
    list(APPEND TEST_SOURCES test_clickhouse.cpp test_clickhouse_compression.cpp test_clickhouse_transport.cpp test_clickhouse_live.cpp)
    if(CNETMOD_ENABLE_ORM AND CNETMOD_ENABLE_HTTP)
        list(APPEND TEST_SOURCES test_application_clickhouse.cpp)
    endif()
endif()
if(CNETMOD_ENABLE_POSTGRESQL)
    list(APPEND TEST_SOURCES test_postgresql_transport.cpp)
endif()
if(NOT CNETMOD_ENABLE_ORM OR NOT CNETMOD_ENABLE_MYSQL)
    list(REMOVE_ITEM TEST_SOURCES test_orm_json_result_map.cpp test_orm_sharding_sessions.cpp)
endif()
if(NOT CNETMOD_ENABLE_ORM)
    list(FILTER TEST_SOURCES EXCLUDE REGEX
        "^(test_orm_|test_database_datetime\\.cpp$)")
endif()

if(CNETMOD_ENABLE_REDIS AND CNETMOD_ENABLE_HTTP)
    list(APPEND TEST_SOURCES test_application_redis.cpp test_application_redis_live.cpp
        test_application_redis_cluster_live.cpp)
endif()

if(CNETMOD_HAS_SSL)
    find_program(CNETMOD_TEST_OPENSSL_EXECUTABLE NAMES openssl)
    if(CNETMOD_TEST_OPENSSL_EXECUTABLE)
        list(APPEND TEST_SOURCES test_ssl_shutdown.cpp)
        if(CNETMOD_ENABLE_REDIS)
            list(APPEND TEST_SOURCES test_redis_tls.cpp)
        endif()
    else()
        message(STATUS "TLS runtime tests unavailable: openssl fixture generator not found")
    endif()
endif()

# Most unit tests exercise one physical component.  Linking every executable to
# the compatibility `cnetmod_core`/`cnetmod::all` facade makes even a coroutine
# test build every enabled protocol and Application integration.  Keep the
# facade unchanged for downstream compatibility, but make test dependencies
# express their real architectural boundary.
function(cnetmod_test_component TEST_NAME OUTPUT_VARIABLE)
    set(_target cnetmod_runtime)

    if(TEST_NAME STREQUAL "test_application_result")
        # R<T, E> is exported by cnetmod.utils and belongs to the runtime
        # component.  Keep this test available in the protocol-free core
        # profile where cnetmod_application is intentionally not built.
        set(_target cnetmod_runtime)
    elseif(TEST_NAME MATCHES "^test_application($|_)" OR
            TEST_NAME MATCHES "^(test_configuration_properties|test_json_template|test_otlp_exporter|test_http_observability|test_http_disabled_overhead|test_grpc)$")
        set(_target cnetmod_application)
    elseif(TEST_NAME MATCHES
            "^(test_orm_json_result_map|test_orm_sharding_sessions|test_orm_tenant_live)$")
        # These repository/session tests import the concrete MySQL adapter in
        # addition to provider-neutral ORM modules. cnetmod_mysql already
        # carries the cnetmod_orm dependency when ORM is enabled.
        set(_target cnetmod_mysql)
    elseif(TEST_NAME MATCHES "^(test_database_datetime|test_orm_)")
        set(_target cnetmod_orm)
    elseif(TEST_NAME STREQUAL "test_quic_early_data")
        # This QUIC scenario also exercises the HTTP/3 server/session layer.
        set(_target cnetmod_http)
    elseif(TEST_NAME MATCHES "^test_quic")
        set(_target cnetmod_quic)
    elseif(TEST_NAME MATCHES "^(test_http|http_client_test|test_metrics|test_tfb)")
        set(_target cnetmod_http)
    elseif(TEST_NAME MATCHES "^test_openai")
        # The suite also validates the Application-owned OpenAI observability
        # adapter, not only the wire protocol component.
        set(_target cnetmod_application)
    elseif(TEST_NAME MATCHES "^test_grpc")
        set(_target cnetmod_grpc)
    elseif(TEST_NAME MATCHES "^test_redis")
        set(_target cnetmod_redis)
    elseif(TEST_NAME MATCHES "^test_clickhouse")
        set(_target cnetmod_clickhouse)
    elseif(TEST_NAME MATCHES "^test_postgresql")
        set(_target cnetmod_postgresql)
    elseif(TEST_NAME MATCHES "^test_mysql")
        set(_target cnetmod_mysql)
    elseif(TEST_NAME MATCHES "^test_mongodb")
        set(_target cnetmod_mongodb)
    elseif(TEST_NAME MATCHES "^test_websocket")
        set(_target cnetmod_websocket)
    elseif(TEST_NAME MATCHES "^test_mqtt")
        set(_target cnetmod_mqtt)
    elseif(TEST_NAME MATCHES "^test_raft")
        set(_target cnetmod_raft)
    elseif(TEST_NAME STREQUAL "test_dns")
        set(_target cnetmod_dns)
    elseif(TEST_NAME STREQUAL "test_coap")
        set(_target cnetmod_coap)
    elseif(TEST_NAME MATCHES "^test_socks5")
        set(_target cnetmod_socks5)
    elseif(TEST_NAME MATCHES "^test_mail")
        set(_target cnetmod_mail)
    endif()

    if(NOT TARGET ${_target})
        message(FATAL_ERROR
            "Test ${TEST_NAME} requires unavailable component ${_target}")
    endif()
    set(${OUTPUT_VARIABLE} ${_target} PARENT_SCOPE)
endfunction()

foreach(test_src ${TEST_SOURCES})
    # Extract test name from filename (e.g. test_task.cpp → test_task)
    get_filename_component(test_name ${test_src} NAME_WE)

    add_executable(${test_name} ${test_src})
    if(test_name STREQUAL "test_quic_in_place_crypto")
        # Same-primary-module implementation unit exercises private crypto.
        # Keep it out of TEST_SOURCES: only the main produces a test target.
        target_sources(${test_name} PRIVATE test_quic_in_place_crypto_impl.cpp)
    endif()
    list(APPEND CNETMOD_TEST_TARGETS ${test_name})
    if(test_name STREQUAL "test_http_disabled_overhead")
        set_property(TARGET ${test_name} PROPERTY
            CNETMOD_DISABLE_MIMALLOC TRUE)
    endif()
    if(test_name STREQUAL "test_mimalloc_runtime")
        # Standalone process-level allocator verification. It deliberately
        # does not inherit mimalloc through a cnetmod library target.
    elseif(test_name STREQUAL "test_c_api")
        target_link_libraries(${test_name} PRIVATE cnetmod_c)
    else()
        cnetmod_test_component(${test_name} test_component)
        target_link_libraries(${test_name} PRIVATE ${test_component})
    endif()
    # The disabled-overhead suite owns the process allocator so it can count
    # exact new/delete calls. Every other executable, including the dedicated
    # mimalloc runtime check, uses the production allocator path.
    if(CNETMOD_USING_MIMALLOC AND
            NOT test_name STREQUAL "test_http_disabled_overhead")
        cnetmod_enable_mimalloc(${test_name})
    endif()
    target_include_directories(${test_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    set_property(TARGET ${test_name} PROPERTY
        CXX_MODULE_GENERATION_MODE "SEPARATE"
    )

    if(MSVC AND test_name STREQUAL "test_http_disabled_overhead")
        # This executable replaces the process allocator and asserts exact
        # allocation counts. MSVC Debug's iterator-proxy allocations are a
        # different ABI and can deadlock inside the STL when fault-injected;
        # the production allocator contract is therefore a Release gate.
        add_test(NAME ${test_name} COMMAND ${test_name} CONFIGURATIONS Release)
    else()
        add_test(NAME ${test_name} COMMAND ${test_name})
    endif()
    if(test_name STREQUAL "test_clickhouse_live" OR test_name STREQUAL "test_application_clickhouse")
        set_tests_properties(${test_name} PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
    elseif(test_name MATCHES "^test_clickhouse")
        set_tests_properties(${test_name} PROPERTIES TIMEOUT 30)
    endif()
    if(test_name MATCHES "^test_application($|_)")
        set_tests_properties(${test_name} PROPERTIES TIMEOUT 60)
    endif()
endforeach()

# MSVC's module frontend retains the imported JSON/OpenAI semantic graph while
# compiling a translation unit.  The complete OpenAI suite is intentionally
# kept in one source file for shared fixtures, but compiling all 90+ cases in a
# single cl.exe process can exceed 40 GiB.  Build bounded preprocessor shards on
# MSVC so each process sees only one bounded group.  Clang/GCC keep the single
# target to avoid multiplying total build work on toolchains without this peak.
if(MSVC AND TARGET test_openai)
    target_compile_definitions(test_openai PRIVATE
        CNETMOD_OPENAI_TEST_GROUP=1)
    set(_cnetmod_openai_test_shards
        retrieval
        runtime
        memory
        agentic
        ingestion
        transport
        import
        model
        agent_tools
        tool_search
        skill_loader
        retrieval_store
        telemetry_events
        telemetry_scopes
        resilience
        memory_trim
        service
        mcp_client
        mcp_tools
        parsers
        evaluation)
    foreach(_index RANGE 0 20)
        math(EXPR _group "${_index} + 2")
        list(GET _cnetmod_openai_test_shards ${_index} _suffix)
        set(_target "test_openai_${_suffix}")
        add_executable(${_target} test_openai.cpp)
        target_link_libraries(${_target} PRIVATE cnetmod_application)
        target_include_directories(${_target} PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR})
        target_compile_definitions(${_target} PRIVATE
            CNETMOD_OPENAI_TEST_GROUP=${_group})
        set_property(TARGET ${_target} PROPERTY
            CXX_MODULE_GENERATION_MODE "SEPARATE")
        add_test(NAME ${_target} COMMAND ${_target})
        list(APPEND CNETMOD_TEST_TARGETS ${_target})
    endforeach()
    unset(_cnetmod_openai_test_shards)
    unset(_index)
    unset(_group)
    unset(_suffix)
    unset(_target)
endif()

# One dependency root lets Visual Studio evaluate the module graph once while
# validating the public HTTP aggregate, its partitions, middleware,
# observability adapters, and protocol consumers. Passing many --target values
# makes CMake invoke MSBuild repeatedly and rescans the complete module graph.
set(_cnetmod_http_arch_test_candidates
    test_http_types
    test_http_tracing
    test_http_observability
    test_http_disabled_overhead
    test_otlp_exporter
    test_http_request
    test_http_response
    test_http_compress
    test_http_parser
    test_http_router
    test_http_sse
    test_http_swagger
    test_http_utils
    http_client_test
    test_http_ip_filter
    test_http_jwt_auth
    test_grpc
    test_openai
    test_openai_import
    test_openai_model
    test_openai_resilience)
set(_cnetmod_http_arch_test_targets)
foreach(_target IN LISTS _cnetmod_http_arch_test_candidates)
    if(TARGET ${_target})
        list(APPEND _cnetmod_http_arch_test_targets ${_target})
    endif()
endforeach()
if(_cnetmod_http_arch_test_targets)
    add_custom_target(cnetmod_http_arch_tests
        DEPENDS ${_cnetmod_http_arch_test_targets})
endif()
unset(_cnetmod_http_arch_test_candidates)
unset(_cnetmod_http_arch_test_targets)
unset(_target)

if(TARGET test_application_redis_live)
    set_tests_properties(test_application_redis_live PROPERTIES
        TIMEOUT 30 SKIP_RETURN_CODE 77 LABELS "integration;redis")
endif()

if(TARGET test_application_redis_cluster_live)
    set_tests_properties(test_application_redis_cluster_live PROPERTIES
        TIMEOUT 45 SKIP_RETURN_CODE 77 LABELS "integration;redis;cluster")
endif()

if(TARGET test_mongodb_lifecycle)
    set_tests_properties(test_mongodb_lifecycle PROPERTIES TIMEOUT 5)
endif()
if(TARGET test_orm_json_result_map)
    set_tests_properties(test_orm_json_result_map PROPERTIES TIMEOUT 15)
endif()
if(TARGET test_mysql_pool)
    set_tests_properties(test_mysql_pool PROPERTIES TIMEOUT 5 LABELS "mysql;pool")
endif()
if(TARGET test_application_mysql_live)
    if(CNETMOD_ENABLE_ORM)
        target_compile_definitions(test_application_mysql_live PRIVATE CNETMOD_TEST_HAS_ORM=1)
    endif()
    set_tests_properties(test_application_mysql_live PROPERTIES
        TIMEOUT 30 SKIP_RETURN_CODE 77 LABELS "integration;mysql")
endif()
if(TARGET test_application_mysql_sharding_live)
    set_tests_properties(test_application_mysql_sharding_live PROPERTIES
        TIMEOUT 45 SKIP_RETURN_CODE 77 LABELS "integration;mysql;sharding")
endif()

if(TARGET test_otlp_exporter AND CNETMOD_ENABLE_ORM)
    target_compile_definitions(test_otlp_exporter PRIVATE CNETMOD_TEST_ORM=1)
endif()

if(TARGET test_ssl_shutdown)
    set(redis_tls_fixture "${CMAKE_CURRENT_BINARY_DIR}/redis-tls-fixture")
    add_custom_command(
        OUTPUT "${redis_tls_fixture}/cert.pem" "${redis_tls_fixture}/key.pem"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${redis_tls_fixture}"
        COMMAND "${CNETMOD_TEST_OPENSSL_EXECUTABLE}" req -x509 -newkey rsa:2048
            -config "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/redis-tls.cnf"
            -nodes -days 3650 -subj /CN=localhost
            -addext "subjectAltName=IP:127.0.0.1,DNS:localhost"
            -keyout "${redis_tls_fixture}/key.pem" -out "${redis_tls_fixture}/cert.pem"
        COMMENT "Generating shared disposable TLS test identity"
        DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/fixtures/redis-tls.cnf"
        VERBATIM)
    add_custom_target(redis_tls_test_identity DEPENDS
        "${redis_tls_fixture}/cert.pem" "${redis_tls_fixture}/key.pem")
    add_dependencies(test_ssl_shutdown redis_tls_test_identity)
    target_compile_definitions(test_ssl_shutdown PRIVATE
        CNETMOD_TLS_TEST_CERT="${redis_tls_fixture}/cert.pem"
        CNETMOD_TLS_TEST_KEY="${redis_tls_fixture}/key.pem")
    set_tests_properties(test_ssl_shutdown PROPERTIES TIMEOUT 5 LABELS "tls;shutdown")
    if(TARGET test_http_sse)
        add_dependencies(test_http_sse redis_tls_test_identity)
        target_compile_definitions(test_http_sse PRIVATE
            CNETMOD_HTTP_SSE_TEST_CERT="${redis_tls_fixture}/cert.pem"
            CNETMOD_HTTP_SSE_TEST_KEY="${redis_tls_fixture}/key.pem")
    endif()
    if(TARGET test_redis_tls)
        add_dependencies(test_redis_tls redis_tls_test_identity)
        target_compile_definitions(test_redis_tls PRIVATE
            CNETMOD_REDIS_TEST_CERT="${redis_tls_fixture}/cert.pem"
            CNETMOD_REDIS_TEST_KEY="${redis_tls_fixture}/key.pem")
        set_tests_properties(test_redis_tls PROPERTIES TIMEOUT 15)
    endif()
endif()

if(MSVC AND TARGET test_raft)
    target_compile_options(test_raft PRIVATE /Od /Ob0)
endif()
if(TARGET test_raft)
    # The five-node TCP chaos cases intentionally consume the event loop and
    # several loopback sockets.  The executable contains all Raft cases, and
    # its slow scenarios have their own bounded watchdogs.  A 60-second CTest
    # limit can kill a healthy macOS Debug run before the final watchdog-backed
    # case starts, so the outer process budget must cover the complete suite.
    set_tests_properties(test_raft PROPERTIES RUN_SERIAL TRUE TIMEOUT 180)
endif()

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME json_backend_boundary
        COMMAND Python3::Interpreter
            "${PROJECT_SOURCE_DIR}/tools/check_json_boundary.py")
endif()
if(TARGET test_orm_tenant_live)
    set_tests_properties(test_orm_tenant_live PROPERTIES
        TIMEOUT 45 SKIP_RETURN_CODE 77 LABELS "integration;mysql;orm;tenant")
endif()
if(Python3_Interpreter_FOUND AND CNETMOD_ENABLE_MAIL)
    add_executable(smtp_bench_server
        "${CMAKE_CURRENT_SOURCE_DIR}/../mail/smtp_bench_server.cpp")
    list(APPEND CNETMOD_TEST_TARGETS smtp_bench_server)
    target_link_libraries(smtp_bench_server PRIVATE cnetmod_core)
    target_include_directories(smtp_bench_server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    set_property(TARGET smtp_bench_server PROPERTY
        CXX_MODULE_GENERATION_MODE "SEPARATE")

    add_executable(smtp_interop_server
        "${CMAKE_CURRENT_SOURCE_DIR}/../mail/smtp_interop_server.cpp")
    add_executable(smtp_interop_client
        "${CMAKE_CURRENT_SOURCE_DIR}/../mail/smtp_interop_client.cpp")
    list(APPEND CNETMOD_TEST_TARGETS smtp_interop_server smtp_interop_client)
    target_link_libraries(smtp_interop_server PRIVATE cnetmod_core)
    target_link_libraries(smtp_interop_client PRIVATE cnetmod_core)
    target_include_directories(smtp_interop_server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    target_include_directories(smtp_interop_client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    set_property(TARGET smtp_interop_server PROPERTY
        CXX_MODULE_GENERATION_MODE "SEPARATE")
    set_property(TARGET smtp_interop_client PROPERTY
        CXX_MODULE_GENERATION_MODE "SEPARATE")
    add_test(
        NAME python_smtp_semantic_interop
        COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/../mail/smtp_semantic_interop.py"
            "$<TARGET_FILE:smtp_interop_server>"
            "$<TARGET_FILE:smtp_interop_client>")
    set_tests_properties(python_smtp_semantic_interop PROPERTIES
        SKIP_RETURN_CODE 77)

endif()

if(Python3_Interpreter_FOUND AND CNETMOD_ENABLE_HTTP)
    add_executable(http2_interop_server
        "${CMAKE_CURRENT_SOURCE_DIR}/../http2/h2_interop_server.cpp")
    add_executable(http2_client_interop
        "${CMAKE_CURRENT_SOURCE_DIR}/../http2/h2_client_interop.cpp")
    list(APPEND CNETMOD_TEST_TARGETS http2_interop_server)
    list(APPEND CNETMOD_TEST_TARGETS http2_client_interop)
    target_link_libraries(http2_interop_server PRIVATE cnetmod_core)
    target_link_libraries(http2_client_interop PRIVATE cnetmod_core)
    set_property(TARGET http2_interop_server PROPERTY CXX_MODULE_GENERATION_MODE "SEPARATE")
    set_property(TARGET http2_client_interop PROPERTY CXX_MODULE_GENERATION_MODE "SEPARATE")
    add_test(NAME python_h2_interop
        COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/../http2/h2_interop.py"
                "$<TARGET_FILE:http2_interop_server>")
    set_tests_properties(python_h2_interop PROPERTIES SKIP_RETURN_CODE 77)
endif()
if(Python3_Interpreter_FOUND AND TARGET test_grpc_proto)
    add_executable(grpc_interop_server
        "${CMAKE_CURRENT_SOURCE_DIR}/../grpc/grpc_interop_server.cpp"
    )
    list(APPEND CNETMOD_TEST_TARGETS grpc_interop_server)
    target_link_libraries(grpc_interop_server PRIVATE cnetmod_core)
    target_include_directories(grpc_interop_server PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    set_property(TARGET grpc_interop_server PROPERTY
        CXX_MODULE_GENERATION_MODE "SEPARATE"
    )

    add_executable(grpc_interop_client
        "${CMAKE_CURRENT_SOURCE_DIR}/../grpc/grpc_interop_client.cpp"
    )
    list(APPEND CNETMOD_TEST_TARGETS grpc_interop_client)
    target_link_libraries(grpc_interop_client PRIVATE cnetmod_core)
    target_include_directories(grpc_interop_client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    set_property(TARGET grpc_interop_client PROPERTY
        CXX_MODULE_GENERATION_MODE "SEPARATE"
    )

    add_test(
        NAME test_grpc_proto_python
        COMMAND Python3::Interpreter
            "${CMAKE_CURRENT_SOURCE_DIR}/../grpc/grpc_proto_interop.py"
            "$<TARGET_FILE:test_grpc_proto>"
            "${CMAKE_CURRENT_SOURCE_DIR}/../grpc/echo.proto"
    )
    add_test(
        NAME test_grpc_codegen_python
        COMMAND Python3::Interpreter
            "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/grpc_cppm_codegen.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/../grpc/echo.proto"
            --module cnetmod.testing.echo.grpc
    )
    add_test(
        NAME test_grpc_codegen_contract_python
        COMMAND Python3::Interpreter
            "${CMAKE_CURRENT_SOURCE_DIR}/../grpc/grpc_codegen_test.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/../../tools/grpc_cppm_codegen.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/../grpc/echo.proto"
    )
    add_test(
        NAME test_grpcio_cross_process_interop_python
        COMMAND Python3::Interpreter
            "${CMAKE_CURRENT_SOURCE_DIR}/../grpc/grpcio_cross_process_interop.py"
            "$<TARGET_FILE:grpc_interop_server>"
            "${CMAKE_CURRENT_SOURCE_DIR}/../grpc/echo.proto"
    )
    add_test(
        NAME test_grpcio_server_to_cnetmod_client_interop_python
        COMMAND Python3::Interpreter
            "${CMAKE_CURRENT_SOURCE_DIR}/../grpc/grpcio_server_to_cnetmod_client_interop.py"
            "$<TARGET_FILE:grpc_interop_client>"
            "${CMAKE_CURRENT_SOURCE_DIR}/../grpc/echo.proto"
    )
    set_tests_properties(
        test_grpcio_cross_process_interop_python
        test_grpcio_server_to_cnetmod_client_interop_python
        PROPERTIES SKIP_RETURN_CODE 77
    )
endif()

if(TARGET test_redis)
    set_tests_properties(test_redis PROPERTIES TIMEOUT 15)
endif()
if(TARGET test_application_redis)
    set_tests_properties(test_application_redis PROPERTIES TIMEOUT 15)
endif()
if(TARGET test_http_observability)
    set_tests_properties(test_http_observability PROPERTIES TIMEOUT 15)
endif()
if(TARGET test_http_disabled_overhead)
    set_tests_properties(test_http_disabled_overhead PROPERTIES TIMEOUT 60)
    if(CNETMOD_ENABLE_ORM)
        target_compile_definitions(test_http_disabled_overhead PRIVATE CNETMOD_TEST_ORM=1)
    endif()
endif()
if(Python3_Interpreter_FOUND AND CNETMOD_ENABLE_MQTT)
    add_executable(mqtt_interop_broker
        "${CMAKE_CURRENT_SOURCE_DIR}/../mqtt/mqtt_interop_broker.cpp"
    )
    list(APPEND CNETMOD_TEST_TARGETS mqtt_interop_broker)
    target_link_libraries(mqtt_interop_broker PRIVATE cnetmod_core)
    target_include_directories(mqtt_interop_broker PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    set_property(TARGET mqtt_interop_broker PROPERTY
        CXX_MODULE_GENERATION_MODE "SEPARATE"
    )

    add_executable(mqtt_interop_client
        "${CMAKE_CURRENT_SOURCE_DIR}/../mqtt/mqtt_interop_client.cpp"
    )
    list(APPEND CNETMOD_TEST_TARGETS mqtt_interop_client)
    target_link_libraries(mqtt_interop_client PRIVATE cnetmod_core)
    target_include_directories(mqtt_interop_client PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    set_property(TARGET mqtt_interop_client PROPERTY
        CXX_MODULE_GENERATION_MODE "SEPARATE"
    )

    add_test(
        NAME test_paho_mqtt_interop_python
        COMMAND Python3::Interpreter
            "${CMAKE_CURRENT_SOURCE_DIR}/../mqtt/paho_mqtt_interop.py"
            "$<TARGET_FILE:mqtt_interop_broker>"
    )
    add_test(
        NAME test_paho_mqtt_weaknet_python
        COMMAND Python3::Interpreter
            "${CMAKE_CURRENT_SOURCE_DIR}/../mqtt/paho_mqtt_weaknet.py"
            "$<TARGET_FILE:mqtt_interop_broker>"
    )
    if(CNETMOD_HAS_SSL)
        add_test(
            NAME test_paho_mqtt_tls_interop_python
            COMMAND Python3::Interpreter
                "${CMAKE_CURRENT_SOURCE_DIR}/../mqtt/paho_mqtt_interop.py"
                "$<TARGET_FILE:mqtt_interop_broker>"
                --tls
                "${redis_tls_fixture}/cert.pem"
                "${redis_tls_fixture}/key.pem"
        )
        add_test(
            NAME test_paho_mqtt_tls_weaknet_python
            COMMAND Python3::Interpreter
                "${CMAKE_CURRENT_SOURCE_DIR}/../mqtt/paho_mqtt_weaknet.py"
                "$<TARGET_FILE:mqtt_interop_broker>"
                --tls
                "${redis_tls_fixture}/cert.pem"
                "${redis_tls_fixture}/key.pem"
        )
    endif()
    add_test(
        NAME test_amqtt_broker_to_cnetmod_client_interop_python
        COMMAND Python3::Interpreter
            "${CMAKE_CURRENT_SOURCE_DIR}/../mqtt/amqtt_broker_interop.py"
            "$<TARGET_FILE:mqtt_interop_client>"
    )
endif()

# HTTP/3 targets and the only HTTP/3 interoperability CTest are owned by
if(TARGET test_dns_cancellation)
    target_link_options(test_dns_cancellation PRIVATE "LINKER:--wrap=getaddrinfo")
    set_tests_properties(test_dns_cancellation PROPERTIES TIMEOUT 15)
endif()
if(TARGET test_udp_sendto_on_cancel)
    set_tests_properties(test_udp_sendto_on_cancel PROPERTIES TIMEOUT 60)
endif()

# HTTP/3 targets and the only HTTP/3 interoperability CTest are owned by
# testing/http3/CMakeLists.txt.  Keeping a second, older Python runner here
# allowed a process-start check to be confused with protocol interoperability.

set(CNETMOD_TEST_TARGETS ${CNETMOD_TEST_TARGETS} PARENT_SCOPE)

if(TARGET test_mongodb_integration)
    set_tests_properties(test_mongodb_integration PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
endif()

if(Python3_Interpreter_FOUND AND CNETMOD_ENABLE_MONGODB)
    add_test(NAME test_mongodb_architecture COMMAND Python3::Interpreter
        "${CMAKE_CURRENT_SOURCE_DIR}/test_mongodb_architecture.py")
endif()

if(TARGET test_mongodb_integration AND CNETMOD_HAS_SNAPPY)
    add_test(NAME test_mongodb_snappy_integration COMMAND test_mongodb_integration)
    set_tests_properties(test_mongodb_snappy_integration PROPERTIES
        SKIP_RETURN_CODE 77 TIMEOUT 60 ENVIRONMENT "CNETMOD_MONGODB_TEST_SNAPPY=1")
endif()

if(TARGET cnetmod_mongodb_cloud)
    foreach(_test test_mongodb_oidc_providers test_mongodb_aws_providers)
        add_executable(${_test} "${_test}.cpp")
        target_link_libraries(${_test} PRIVATE cnetmod_mongodb_cloud)
        set_property(TARGET ${_test} PROPERTY CXX_MODULE_GENERATION_MODE SEPARATE)
        add_test(NAME ${_test} COMMAND ${_test})
    endforeach()
endif()

if(CNETMOD_ENABLE_MONGODB AND CNETMOD_HAS_SSL AND Python3_Interpreter_FOUND)
    add_executable(mongodb_cloud_handshake_driver mongodb_cloud_handshake_driver.cpp mongodb_cloud_test_certificate.cpp)
    target_link_libraries(mongodb_cloud_handshake_driver PRIVATE cnetmod_mongodb)
    set_property(TARGET mongodb_cloud_handshake_driver PROPERTY CXX_MODULE_GENERATION_MODE SEPARATE)
    add_test(NAME test_mongodb_cloud_handshake COMMAND Python3::Interpreter
        "${CMAKE_CURRENT_SOURCE_DIR}/test_mongodb_cloud_handshake.py" "$<TARGET_FILE:mongodb_cloud_handshake_driver>")
    set_tests_properties(test_mongodb_cloud_handshake PROPERTIES TIMEOUT 90)
endif()

if(CNETMOD_ENABLE_MONGODB)
    add_executable(mongodb_srv_lookup_driver mongodb_srv_lookup_driver.cpp)
    target_link_libraries(mongodb_srv_lookup_driver PRIVATE cnetmod_mongodb)
    set_property(TARGET mongodb_srv_lookup_driver PROPERTY CXX_MODULE_GENERATION_MODE SEPARATE)
endif()

if(TARGET test_mongodb_integration AND CNETMOD_HAS_SNAPPY AND CNETMOD_HAS_ZLIB)
    add_test(NAME test_mongodb_compressor_preference COMMAND test_mongodb_integration)
    set_tests_properties(test_mongodb_compressor_preference PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60
        ENVIRONMENT "CNETMOD_MONGODB_TEST_COMPRESSION_ORDER=1")
endif()

if(TARGET cnetmod_application AND TARGET cnetmod_mongodb_cloud AND CNETMOD_HAS_SNAPPY)
    add_executable(test_application_mongodb_lifecycle test_application_mongodb_lifecycle.cpp)
    target_link_libraries(test_application_mongodb_lifecycle PRIVATE cnetmod_application)
    set_property(TARGET test_application_mongodb_lifecycle PROPERTY CXX_MODULE_GENERATION_MODE SEPARATE)
    add_test(NAME test_application_mongodb_lifecycle COMMAND test_application_mongodb_lifecycle)
    set_tests_properties(test_application_mongodb_lifecycle PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
endif()

if(TARGET cnetmod_application AND TARGET cnetmod_mongodb_cloud AND CNETMOD_HAS_SSL AND Python3_Interpreter_FOUND)
    add_executable(mongodb_application_cloud_driver mongodb_application_cloud_driver.cpp mongodb_cloud_test_certificate.cpp)
    target_link_libraries(mongodb_application_cloud_driver PRIVATE cnetmod_application)
    set_property(TARGET mongodb_application_cloud_driver PROPERTY CXX_MODULE_GENERATION_MODE SEPARATE)
    add_test(NAME test_application_mongodb_cloud COMMAND Python3::Interpreter
        "${CMAKE_CURRENT_SOURCE_DIR}/test_application_mongodb_cloud.py" "$<TARGET_FILE:mongodb_application_cloud_driver>")
    set_tests_properties(test_application_mongodb_cloud PROPERTIES TIMEOUT 90)
endif()
