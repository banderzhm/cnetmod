# ==============================================================================
# cnetmod examples
# ==============================================================================

# In MSVC Debug mode, /RTC1 (runtime checks) combined with import std; template instantiation
# can easily cause single TU object files to exceed 4GB (C1605).
# Remove /RTC1 and replace /Ob0 with /Ob1 in this subdirectory scope:
#   - /Ob1: Allow inlining of marked functions, significantly compressing std utility function bodies
#   - Remove /RTC1: Eliminate instrumentation code for each variable access
# Note: This variable modification only affects targets created in this subdirectory, not propagated to parent
# Note: Debug symbols (/Zi) and debug runtime library (/MDd) are still retained
if(MSVC)
    string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REPLACE "/Ob0"  "/Ob1" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
endif()

# ==============================================================================
# Generate embedded mappers header for account_server_demo
# ==============================================================================
if(CNETMOD_ENABLE_HTTP AND CNETMOD_ENABLE_OPENAI AND
   CNETMOD_ENABLE_MYSQL AND CNETMOD_ENABLE_ORM)
    include(${CMAKE_SOURCE_DIR}/cmake/utils/EmbedMappers.cmake)
    cnetmod_embed_mappers(
        OUTPUT_FILE ${CMAKE_CURRENT_BINARY_DIR}/embedded_mappers.hpp
        MAPPER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/database/mysql/mappers
        NAMESPACE embedded_mappers
    )
endif()

# ==============================================================================
# Core Examples
# ==============================================================================
set(CORE_EXAMPLES
    core/echo_server
    core/async_file
    core/timer_demo
    core/serial_port
)

# SSL examples (only built when OpenSSL is available)
if(CNETMOD_HAS_SSL)
    list(APPEND CORE_EXAMPLES core/ssl_echo_server)
    list(APPEND CORE_EXAMPLES core/dtls_echo_server)
    list(APPEND CORE_EXAMPLES core/dtls_echo_client)
endif()

# ==============================================================================
# Concurrency Examples
# ==============================================================================
set(CONCURRENCY_EXAMPLES
    concurrency/channel_demo
    concurrency/mutex_demo
    concurrency/stdexec_bridge
    concurrency/blocking_bridge_demo
)

# ==============================================================================
# HTTP Examples
# ==============================================================================
set(HTTP_EXAMPLES)
if(CNETMOD_ENABLE_HTTP)
    list(APPEND HTTP_EXAMPLES
        http/http_demo
        http/http2_demo
        http/hight_http
        http/hight_plus_http
        http/multicore_http
        http/client_demo
        http/cookie_demo
    )
    if(CNETMOD_ENABLE_MYSQL)
        list(APPEND HTTP_EXAMPLES http/tfb_benchmark)
        if(CNETMOD_ENABLE_OPENAI AND CNETMOD_ENABLE_ORM)
            list(APPEND HTTP_EXAMPLES http/account_server_demo)
        endif()
    endif()
endif()

# ==============================================================================
# WebSocket Examples
# ==============================================================================
set(WEBSOCKET_EXAMPLES)
if(CNETMOD_ENABLE_WEBSOCKET)
    list(APPEND WEBSOCKET_EXAMPLES
        websocket/ws_demo
        websocket/hight_ws
        websocket/multicore_ws
    )
endif()

# ==============================================================================
# Database Examples
# ==============================================================================
set(DATABASE_EXAMPLES)
if(CNETMOD_ENABLE_MYSQL)
    list(APPEND DATABASE_EXAMPLES database/mysql/mysql_crud)
    if(CNETMOD_ENABLE_ORM)
        list(APPEND DATABASE_EXAMPLES
        database/mysql/mysql_orm
        database/mysql/mysql_transaction
        database/mysql/mysql_xml_mapper
        database/mysql/mysql_xml_complex
        database/mysql/mysql_mybatis_plus_demo
        database/mysql/mysql_transaction_rollback_demo
        )
    endif()
endif()
if(CNETMOD_ENABLE_POSTGRESQL AND CNETMOD_ENABLE_HTTP AND CNETMOD_ENABLE_ORM)
    list(APPEND DATABASE_EXAMPLES database/postgresql/postgresql_production_service)
endif()
if(CNETMOD_ENABLE_MONGODB)
    list(APPEND DATABASE_EXAMPLES database/mongodb/mongodb_production_service)
endif()

# ==============================================================================
# Redis Examples
# ==============================================================================
set(REDIS_EXAMPLES)
if(CNETMOD_ENABLE_REDIS)
    list(APPEND REDIS_EXAMPLES
        redis/redis_client
        redis/redis_pool
        redis/redis_sharded_pool
    )
endif()

# ==============================================================================
# gRPC Examples
# ==============================================================================
set(GRPC_EXAMPLES)
if(CNETMOD_ENABLE_GRPC)
    list(APPEND GRPC_EXAMPLES grpc/security_interceptor)
endif()

# ==============================================================================
# Raft Examples
# ==============================================================================
set(RAFT_EXAMPLES)
if(CNETMOD_ENABLE_RAFT)
    list(APPEND RAFT_EXAMPLES
        raft/redis_cluster
        raft/oss_shared_storage
    )
endif()

# ==============================================================================
# MQTT Examples
# ==============================================================================
set(MQTT_EXAMPLES)
if(CNETMOD_ENABLE_MQTT)
    list(APPEND MQTT_EXAMPLES mqtt/mqtt_demo)
endif()

# ==============================================================================
# Messaging Examples
# ==============================================================================
set(MESSAGING_EXAMPLES)
if(CNETMOD_ENABLE_KAFKA)
    list(APPEND MESSAGING_EXAMPLES kafka/kafka_demo)
endif()
if(CNETMOD_ENABLE_AMQP091)
    list(APPEND MESSAGING_EXAMPLES amqp091/amqp091_demo)
endif()
if(CNETMOD_ENABLE_AMQP10)
    list(APPEND MESSAGING_EXAMPLES amqp10/amqp10_demo)
endif()

# ==============================================================================
# Modbus Examples
# ==============================================================================
set(MODBUS_EXAMPLES)
if(CNETMOD_ENABLE_MODBUS)
    list(APPEND MODBUS_EXAMPLES
        modbus/modbus_demo
        modbus/modbus_converter_demo
        modbus/modbus_rtu_demo
        modbus/modbus_udp_demo
    )
endif()

# ==============================================================================
# CoAP Examples
# ==============================================================================
set(COAP_EXAMPLES)
if(CNETMOD_ENABLE_COAP)
    list(APPEND COAP_EXAMPLES
        coap/coap_interop_server
        coap/coap_interop_client
        coap/coap_multicast_server
        coap/coap_multicast_client
    )
endif()

if(CNETMOD_ENABLE_COAP AND CNETMOD_HAS_SSL)
    list(APPEND COAP_EXAMPLES coap/coaps_interop_server)
    list(APPEND COAP_EXAMPLES coap/coaps_interop_client)
endif()

# ==============================================================================
# Combine all examples
# ==============================================================================
set(ALL_EXAMPLES
    ${CORE_EXAMPLES}
    ${CONCURRENCY_EXAMPLES}
    ${HTTP_EXAMPLES}
    ${WEBSOCKET_EXAMPLES}
    ${DATABASE_EXAMPLES}
    ${REDIS_EXAMPLES}
    ${GRPC_EXAMPLES}
    ${RAFT_EXAMPLES}
    ${MQTT_EXAMPLES}
    ${MESSAGING_EXAMPLES}
    ${MODBUS_EXAMPLES}
    ${COAP_EXAMPLES}
)

# ==============================================================================
# Build all examples
# ==============================================================================
set(CNETMOD_EXAMPLE_TARGETS)

foreach(example_path ${ALL_EXAMPLES})
    # Extract category and name from path (e.g., "core/echo_server" -> "core" and "echo_server")
    get_filename_component(category ${example_path} DIRECTORY)
    get_filename_component(example_name ${example_path} NAME)
    
    # Create target name: example_echo_server, example_redis_client, etc.
    set(target_name example_${example_name})
    
    # Add executable
    add_executable(${target_name} ${example_path}.cpp)
    list(APPEND CNETMOD_EXAMPLE_TARGETS ${target_name})
    target_link_libraries(${target_name} PRIVATE ${CNETMOD_LINK_TARGET})
    set_property(TARGET ${target_name} PROPERTY
        CXX_MODULE_GENERATION_MODE "SEPARATE"
    )
    
    # Set output directory to category subdirectory
    set_target_properties(${target_name} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/examples/${category}"
    )
    
    if(MSVC)
        # Disable debug info + disable LTCG + minimize code to avoid MSVC C1605 (object file exceeds 4GB)
        # /O1 appended at end of AdditionalOptions, overrides MSBuild-generated /Od
        # /RTC1 already removed at top of this subdirectory via string(REPLACE), so /O1 doesn't conflict with /RTC1
        set_property(TARGET ${target_name} PROPERTY
            MSVC_DEBUG_INFORMATION_FORMAT "")
        target_compile_options(${target_name} PRIVATE /bigobj /Zm800 /EHsc /GL- /O1)
        target_link_options(${target_name} PRIVATE /LTCG:OFF /DEBUG:NONE)
    endif()
endforeach()

set(CNETMOD_EXAMPLE_TARGETS ${CNETMOD_EXAMPLE_TARGETS} PARENT_SCOPE)

# ==============================================================================
# Special handling for account_server_demo (needs embedded_mappers.hpp)
# ==============================================================================
if(TARGET example_account_server_demo)
    target_include_directories(example_account_server_demo PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
    # jwt-cpp is header-only and its nlohmann traits include json.hpp from the
    # header context, so both third-party header dependencies belong here.
    cnetmod_link_jwt_cpp(example_account_server_demo)
    cnetmod_link_nlohmann_json(example_account_server_demo)
endif()

# stdexec currently ships as a header-only P2300 implementation rather than a
# C++ module.  Keep this third-party header dependency explicit for the few
# examples that use exec::async_scope or exec::static_thread_pool directly.
foreach(stdexec_header_example
        example_http_demo
        example_ws_demo
        example_mqtt_demo)
    if(TARGET ${stdexec_header_example})
        cnetmod_link_stdexec(${stdexec_header_example})
    endif()
endforeach()

if(MSVC)
    foreach(target_name
        example_async_file
        example_coap_multicast_server
        example_coap_multicast_client
    )
        if(TARGET ${target_name})
            target_compile_options(${target_name} PRIVATE /Od /Ob0)
        endif()
    endforeach()
endif()

# ==============================================================================
# Post-build commands
# ==============================================================================

# Copy mapper XML files once. Keeping this as a single target avoids parallel
# POST_BUILD copy races when several database examples build at the same time.
if(CNETMOD_ENABLE_MYSQL AND CNETMOD_ENABLE_ORM)
add_custom_command(
    OUTPUT ${CMAKE_BINARY_DIR}/examples/database/mysql/mappers/.cnetmod_mappers_copied
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}/examples/database/mysql/mappers
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            ${CMAKE_CURRENT_SOURCE_DIR}/database/mysql/mappers
            ${CMAKE_BINARY_DIR}/examples/database/mysql/mappers
    COMMAND ${CMAKE_COMMAND} -E touch
            ${CMAKE_BINARY_DIR}/examples/database/mysql/mappers/.cnetmod_mappers_copied
    DEPENDS
            ${CMAKE_CURRENT_SOURCE_DIR}/database/mysql/mappers/AiMapper.xml
            ${CMAKE_CURRENT_SOURCE_DIR}/database/mysql/mappers/ClientMapper.xml
            ${CMAKE_CURRENT_SOURCE_DIR}/database/mysql/mappers/ExpenseMapper.xml
            ${CMAKE_CURRENT_SOURCE_DIR}/database/mysql/mappers/FileMapper.xml
            ${CMAKE_CURRENT_SOURCE_DIR}/database/mysql/mappers/ProjectMapper.xml
            ${CMAKE_CURRENT_SOURCE_DIR}/database/mysql/mappers/user_mapper.xml
    COMMENT "Copying mapper XML files"
    VERBATIM
)

add_custom_target(copy_example_mappers
    DEPENDS ${CMAKE_BINARY_DIR}/examples/database/mysql/mappers/.cnetmod_mappers_copied
)

list(APPEND CNETMOD_EXAMPLE_TARGETS copy_example_mappers)

foreach(mysql_mapper_example
        example_mysql_xml_mapper
        example_mysql_xml_complex
        example_mysql_mybatis_plus_demo)
    if(TARGET ${mysql_mapper_example})
        add_dependencies(${mysql_mapper_example} copy_example_mappers)
    endif()
endforeach()
endif()

set(CNETMOD_EXAMPLE_TARGETS ${CNETMOD_EXAMPLE_TARGETS} PARENT_SCOPE)
