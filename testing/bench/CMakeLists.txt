# cnetmod performance benchmarks
# Enable with -DCNETMOD_BUILD_BENCH=ON
# IMPORTANT: Build in Release mode for meaningful results!
#   cmake --build . --config Release

# Remove debug runtime checks for accurate benchmarking
if(MSVC)
    string(REPLACE "/RTC1" "" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REPLACE "/Ob0"  "/Ob2" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
endif()

set(BENCH_TARGETS
    bench_coroutine
    bench_channel
    bench_sync
    bench_io
    bench_http
)

foreach(bench_name ${BENCH_TARGETS})
    add_executable(${bench_name} ${bench_name}.cpp)
    target_link_libraries(${bench_name} PRIVATE cnetmod_core)
    target_include_directories(${bench_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    set_property(TARGET ${bench_name} PROPERTY
        CXX_MODULE_GENERATION_MODE "SEPARATE"
    )

    if(MSVC)
        # Optimize for speed in all configurations
        set_property(TARGET ${bench_name} PROPERTY
            MSVC_DEBUG_INFORMATION_FORMAT "")
        target_compile_options(${bench_name} PRIVATE /bigobj /Zm800 /EHsc /GL- /O2)
        target_link_options(${bench_name} PRIVATE /LTCG:OFF /DEBUG:NONE)
    else()
        target_compile_options(${bench_name} PRIVATE -O3)
    endif()
endforeach()
