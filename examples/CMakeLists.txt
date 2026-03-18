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
include(${CMAKE_SOURCE_DIR}/cmake/utils/EmbedMappers.cmake)

cnetmod_embed_mappers(
    OUTPUT_FILE ${CMAKE_CURRENT_BINARY_DIR}/embedded_mappers.hpp
    MAPPER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/mappers
    NAMESPACE embedded_mappers
)

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
set(HTTP_EXAMPLES
    http/http_demo
    http/http2_demo
    http/hight_http
    http/hight_plus_http
    http/multicore_http
    http/tfb_benchmark
    http/client_demo
    http/cookie_demo
    http/account_server_demo
)

# ==============================================================================
# WebSocket Examples
# ==============================================================================
set(WEBSOCKET_EXAMPLES
    websocket/ws_demo
    websocket/hight_ws
    websocket/multicore_ws
)

# ==============================================================================
# Database Examples
# ==============================================================================
set(DATABASE_EXAMPLES
    database/mysql_crud
    database/mysql_orm
    database/mysql_transaction
    database/mysql_xml_mapper
    database/mysql_xml_complex
    database/mybatis_plus_demo
    database/transaction_simple_test
)

# ==============================================================================
# Redis Examples
# ==============================================================================
set(REDIS_EXAMPLES
    redis/redis_client
    redis/redis_pool
    redis/redis_sharded_pool
)

# ==============================================================================
# MQTT Examples
# ==============================================================================
set(MQTT_EXAMPLES
    mqtt/mqtt_demo
)

# ==============================================================================
# Modbus Examples
# ==============================================================================
set(MODBUS_EXAMPLES
    modbus/modbus_demo
    modbus_converter_demo
)

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
    ${MQTT_EXAMPLES}
    ${MODBUS_EXAMPLES}
)

# ==============================================================================
# Build all examples
# ==============================================================================
foreach(example_path ${ALL_EXAMPLES})
    # Extract category and name from path (e.g., "core/echo_server" -> "core" and "echo_server")
    get_filename_component(category ${example_path} DIRECTORY)
    get_filename_component(example_name ${example_path} NAME)
    
    # Create target name: example_echo_server, example_redis_client, etc.
    set(target_name example_${example_name})
    
    # Add executable
    add_executable(${target_name} ${example_path}.cpp)
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

# ==============================================================================
# Special handling for account_server_demo (needs embedded_mappers.hpp)
# ==============================================================================
target_include_directories(example_account_server_demo PRIVATE ${CMAKE_CURRENT_BINARY_DIR})

# ==============================================================================
# Post-build commands
# ==============================================================================

# Copy mapper XML files to build directory for mysql_xml_mapper example
add_custom_command(TARGET example_mysql_xml_mapper POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/mappers
    ${CMAKE_BINARY_DIR}/examples/database/mappers
    COMMENT "Copying mapper XML files for mysql_xml_mapper"
)

# Copy mapper XML files for mysql_xml_complex example
add_custom_command(TARGET example_mysql_xml_complex POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/mappers
    ${CMAKE_BINARY_DIR}/examples/database/mappers
    COMMENT "Copying mapper XML files for mysql_xml_complex"
)

# Copy mapper XML files for mybatis_plus_demo example
add_custom_command(TARGET example_mybatis_plus_demo POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${CMAKE_CURRENT_SOURCE_DIR}/mappers
    ${CMAKE_BINARY_DIR}/examples/database/mappers
    COMMENT "Copying mapper XML files for mybatis_plus_demo"
)