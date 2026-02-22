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

set(EXAMPLE_TARGETS
    echo_server
    async_file
    timer_demo
    channel_demo
    mutex_demo
    stdexec_bridge
    redis_client
    mysql_crud
    mysql_orm
    http_demo
    ws_demo
    hight_http
    hight_ws
    hight_plus_http
    serial_port
    multicore_http
    multicore_ws
    blocking_bridge_demo
        mqtt_demo
    tfb_benchmark
)

# SSL examples (only built when OpenSSL is available)
if(CNETMOD_HAS_SSL)
    list(APPEND EXAMPLE_TARGETS ssl_echo_server)
endif()

foreach(example ${EXAMPLE_TARGETS})
    add_executable(example_${example} ${example}.cpp)
    target_link_libraries(example_${example} PRIVATE ${CNETMOD_LINK_TARGET})
    set_property(TARGET example_${example} PROPERTY
        CXX_MODULE_GENERATION_MODE "SEPARATE"
    )
    if(MSVC)
        # Disable debug info + disable LTCG + minimize code to avoid MSVC C1605 (object file exceeds 4GB)
        # /O1 appended at end of AdditionalOptions, overrides MSBuild-generated /Od
        # /RTC1 already removed at top of this subdirectory via string(REPLACE), so /O1 doesn't conflict with /RTC1
        set_property(TARGET example_${example} PROPERTY
            MSVC_DEBUG_INFORMATION_FORMAT "")
        target_compile_options(example_${example} PRIVATE /bigobj /Zm800 /EHsc /GL- /O1)
        target_link_options(example_${example} PRIVATE /LTCG:OFF /DEBUG:NONE)
    endif()
endforeach()