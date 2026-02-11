# ==============================================================================
# cnetmod examples
# ==============================================================================

set(EXAMPLE_TARGETS
    echo_server
    async_file
    timer_demo
    channel_demo
    mutex_demo
    stdexec_bridge
    redis_client
)

foreach(example ${EXAMPLE_TARGETS})
    add_executable(example_${example} ${example}.cpp)
    target_link_libraries(example_${example} PRIVATE cnetmod_core)
    set_property(TARGET example_${example} PROPERTY
        CXX_MODULE_GENERATION_MODE "SEPARATE"
    )
    # MSVC C++ Modules COMDAT workaround: disable LTCG
    if(MSVC)
        target_compile_options(example_${example} PRIVATE /GL-)
        target_link_options(example_${example} PRIVATE /LTCG:OFF)
    endif()
endforeach()
