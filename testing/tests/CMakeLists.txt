# cnetmod test configuration
# Enable with -DCNETMOD_BUILD_TESTS=ON

enable_testing()

# Test source files — each file produces an independent executable
set(TEST_SOURCES
    test_task.cpp
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
    test_http_request.cpp
    test_http_response.cpp
    test_http_parser.cpp
    test_http_router.cpp
    test_http_sse.cpp
    test_http_swagger.cpp
    test_http_utils.cpp
    test_http_ip_filter.cpp
    test_http2_hpack.cpp
    test_dns.cpp
    test_coap.cpp
    test_grpc.cpp
    test_grpc_proto.cpp
    test_mqtt.cpp
    test_metrics.cpp
    test_tfb_compliance.cpp
    test_socks5.cpp
    test_mail.cpp
    test_redis.cpp
)

set(CNETMOD_TEST_TARGETS)

foreach(test_src ${TEST_SOURCES})
    # Extract test name from filename (e.g. test_task.cpp → test_task)
    get_filename_component(test_name ${test_src} NAME_WE)

    add_executable(${test_name} ${test_src})
    list(APPEND CNETMOD_TEST_TARGETS ${test_name})
    target_link_libraries(${test_name} PRIVATE cnetmod_core)
    target_include_directories(${test_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    set_property(TARGET ${test_name} PROPERTY
        CXX_MODULE_GENERATION_MODE "SEPARATE"
    )

    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()

if(MSVC AND TARGET test_raft)
    target_compile_options(test_raft PRIVATE /Od /Ob0)
endif()

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
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
endif()

if(Python3_Interpreter_FOUND)
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
                "${CMAKE_CURRENT_SOURCE_DIR}/../../examples/test_ssl/cert.pem"
                "${CMAKE_CURRENT_SOURCE_DIR}/../../examples/test_ssl/key.pem"
        )
        add_test(
            NAME test_paho_mqtt_tls_weaknet_python
            COMMAND Python3::Interpreter
                "${CMAKE_CURRENT_SOURCE_DIR}/../mqtt/paho_mqtt_weaknet.py"
                "$<TARGET_FILE:mqtt_interop_broker>"
                --tls
                "${CMAKE_CURRENT_SOURCE_DIR}/../../examples/test_ssl/cert.pem"
                "${CMAKE_CURRENT_SOURCE_DIR}/../../examples/test_ssl/key.pem"
        )
    endif()
    add_test(
        NAME test_amqtt_broker_to_cnetmod_client_interop_python
        COMMAND Python3::Interpreter
            "${CMAKE_CURRENT_SOURCE_DIR}/../mqtt/amqtt_broker_interop.py"
            "$<TARGET_FILE:mqtt_interop_client>"
    )
endif()

set(CNETMOD_TEST_TARGETS ${CNETMOD_TEST_TARGETS} PARENT_SCOPE)
