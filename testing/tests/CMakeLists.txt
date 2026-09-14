# cnetmod test configuration
# Enable with -DCNETMOD_BUILD_TESTS=ON

enable_testing()

# Test source files — each file produces an independent executable
set(TEST_SOURCES
    test_instrumentation.cpp
    test_metric_aggregation.cpp
    test_flat_map.cpp
    test_charconv.cpp
    test_concurrent_containers.cpp
    test_application_result.cpp
    test_application.cpp
    test_application_integrations.cpp
    test_deadline.cpp
    test_task_group.cpp
    test_task.cpp
    test_lazy.cpp
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
    test_http_parser.cpp
    test_http_router.cpp
    test_http_sse.cpp
    test_http_swagger.cpp
    test_http_utils.cpp
    http_client_test.cpp
    test_http_ip_filter.cpp
    test_http2_hpack.cpp
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
    test_mongodb_wire.cpp
    test_orm_json_result_map.cpp

    # QUIC/HTTP3 tests
    test_quic_varint.cpp
    test_quic_frame.cpp
    test_quic_packet.cpp
    test_quic_stream.cpp
    test_quic_loss_detection.cpp
    test_quic_congestion.cpp
    test_quic_early_data.cpp
    test_http3_qpack.cpp
    test_http3_frame.cpp
)

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
        test_application_integrations.cpp test_otlp_exporter.cpp http_client_test.cpp)
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
    list(REMOVE_ITEM TEST_SOURCES test_redis.cpp)
endif()
if(NOT CNETMOD_ENABLE_MONGODB)
    list(REMOVE_ITEM TEST_SOURCES test_mongodb_wire.cpp)
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
if(CNETMOD_ENABLE_MYSQL)
    list(APPEND TEST_SOURCES test_mysql_pool.cpp)
endif()
if(NOT CNETMOD_ENABLE_ORM OR NOT CNETMOD_ENABLE_MYSQL)
    list(REMOVE_ITEM TEST_SOURCES test_orm_json_result_map.cpp)
endif()

if(CNETMOD_ENABLE_REDIS AND CNETMOD_ENABLE_HTTP)
    list(APPEND TEST_SOURCES test_application_redis.cpp test_application_redis_live.cpp)
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

foreach(test_src ${TEST_SOURCES})
    # Extract test name from filename (e.g. test_task.cpp → test_task)
    get_filename_component(test_name ${test_src} NAME_WE)

    add_executable(${test_name} ${test_src})
    list(APPEND CNETMOD_TEST_TARGETS ${test_name})
    if(test_name STREQUAL "test_c_api")
        target_link_libraries(${test_name} PRIVATE cnetmod_c)
    else()
        target_link_libraries(${test_name} PRIVATE cnetmod_core)
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
    if(test_name MATCHES "^test_application($|_)")
        set_tests_properties(${test_name} PROPERTIES TIMEOUT 60)
    endif()
endforeach()

if(TARGET test_application_redis_live)
    set_tests_properties(test_application_redis_live PROPERTIES
        TIMEOUT 30 SKIP_RETURN_CODE 77 LABELS "integration;redis")
endif()

if(TARGET test_mongodb_wire)
    set_tests_properties(test_mongodb_wire PROPERTIES TIMEOUT 5)
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

# HTTP/3 targets and the only HTTP/3 interoperability CTest are owned by
# testing/http3/CMakeLists.txt.  Keeping a second, older Python runner here
# allowed a process-start check to be confused with protocol interoperability.

set(CNETMOD_TEST_TARGETS ${CNETMOD_TEST_TARGETS} PARENT_SCOPE)
