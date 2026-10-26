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
    bench_json
    bench_sync
    bench_io
    bench_http
    bench_grpc
    bench_ws
    bench_mqtt
    bench_raft
    crosslang_cnetmod_server
)

if(CNETMOD_ENABLE_POSTGRESQL AND CNETMOD_ENABLE_MYSQL AND CNETMOD_ENABLE_REDIS)
    list(APPEND BENCH_TARGETS bench_database)
endif()

if(CNETMOD_ENABLE_HTTP)
    list(APPEND BENCH_TARGETS bench_http_observation bench_http_baseline bench_http_disabled
        bench_http_parser bench_http_client_pool)
    set(bench_http_baseline_SOURCE bench_http_observation.cpp)
    set(bench_http_disabled_SOURCE bench_http_observation.cpp)
endif()

if(NOT CNETMOD_ENABLE_HTTP)
    list(REMOVE_ITEM BENCH_TARGETS bench_http bench_grpc crosslang_cnetmod_server)
endif()
if(NOT CNETMOD_ENABLE_GRPC)
    list(REMOVE_ITEM BENCH_TARGETS bench_grpc)
else()
    list(APPEND BENCH_TARGETS grpc_arena_server)
endif()
if(NOT CNETMOD_ENABLE_WEBSOCKET)
    list(REMOVE_ITEM BENCH_TARGETS bench_ws)
else()
    list(APPEND BENCH_TARGETS bench_ws_codec bench_ws_server)
endif()
if(NOT CNETMOD_ENABLE_MQTT)
    list(REMOVE_ITEM BENCH_TARGETS bench_mqtt)
endif()
if(NOT CNETMOD_ENABLE_RAFT)
    list(REMOVE_ITEM BENCH_TARGETS bench_raft)
endif()

# io_uring is Linux-only. Do not create this target on macOS/Windows: the
# corresponding C++ module is intentionally absent on those platforms.
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" AND CNETMOD_HAS_IO_URING AND CNETMOD_ENABLE_HTTP)
    list(APPEND BENCH_TARGETS bench_uring_recv)
endif()

# HTTP/3 over QUIC benchmark (requires QUIC + SSL support)
if(CNETMOD_HAS_QUIC AND CNETMOD_HAS_SSL)
    list(APPEND BENCH_TARGETS h3_benchmark arena_h3_server)
endif()

# Map target name to source file (most targets use <name>.cpp)
set(h3_benchmark_SOURCE h3_performance.cpp)

foreach(bench_name ${BENCH_TARGETS})
    # Resolve source file: use override if defined, otherwise <name>.cpp
    set(_src_var "${bench_name}_SOURCE")
    if(DEFINED ${_src_var})
        set(_bench_src "${${_src_var}}")
    else()
        set(_bench_src "${bench_name}.cpp")
    endif()

    add_executable(${bench_name} ${_bench_src})
    if(bench_name STREQUAL "bench_http_baseline")
        target_compile_definitions(${bench_name} PRIVATE CNETMOD_BENCH_RAW_ONLY=1)
    elseif(bench_name STREQUAL "bench_http_disabled")
        target_compile_definitions(${bench_name} PRIVATE CNETMOD_BENCH_DISABLED_ONLY=1)
    endif()
    if(bench_name STREQUAL "bench_database")
        target_link_libraries(${bench_name} PRIVATE cnetmod_postgresql cnetmod_mysql cnetmod_redis)
    elseif(bench_name STREQUAL "grpc_arena_server")
        target_link_libraries(${bench_name} PRIVATE cnetmod_grpc)
    elseif(bench_name MATCHES "^bench_ws_(codec|server)$")
        target_link_libraries(${bench_name} PRIVATE cnetmod_websocket)
    elseif(bench_name STREQUAL "bench_http_client_pool")
        target_link_libraries(${bench_name} PRIVATE cnetmod_http)
    elseif(bench_name STREQUAL "crosslang_cnetmod_server" OR
            bench_name STREQUAL "arena_h3_server")
        # The cross-language server exercises the HTTP stack directly. Keep
        # Application, ORM and unrelated protocols out of the benchmark binary
        # so allocator and transport measurements are not configuration-bound.
        target_link_libraries(${bench_name} PRIVATE cnetmod_http)
    else()
        target_link_libraries(${bench_name} PRIVATE cnetmod_core)
    endif()
    cnetmod_deploy_mimalloc_runtime(${bench_name})
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

if(CNETMOD_BUILD_TESTS AND TARGET grpc_arena_server)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME test_grpc_arena_harness
            COMMAND "${Python3_EXECUTABLE}" -m unittest discover
                -s "${CMAKE_CURRENT_SOURCE_DIR}" -p test_grpc_arena_bench.py)
        add_test(NAME test_grpc_official_analysis
            COMMAND "${Python3_EXECUTABLE}" -m unittest discover
                -s "${CMAKE_CURRENT_SOURCE_DIR}" -p test_analyze_official_grpc.py)
        set(_grpc_arena_tls_args)
        if(CNETMOD_HAS_SSL AND CNETMOD_TEST_OPENSSL_EXECUTABLE)
            list(APPEND _grpc_arena_tls_args --openssl "${CNETMOD_TEST_OPENSSL_EXECUTABLE}")
        endif()
        add_test(NAME test_grpc_arena_smoke
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/grpc_arena_smoke.py"
                "$<TARGET_FILE:grpc_arena_server>" ${_grpc_arena_tls_args})
        set_tests_properties(test_grpc_arena_smoke PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 60)
    endif()
endif()

if(TARGET bench_http_parser)
    if(MSVC AND (CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|amd64|x86_64"))
        target_compile_options(bench_http_parser PRIVATE /arch:AVX2)
    elseif((CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU") AND
           (CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|amd64|x86_64"))
        target_compile_options(bench_http_parser PRIVATE -mavx2)
    endif()
endif()

# HTTP/3 measurements require a separately started peer and pinned machine
# configuration.  They are intentionally not CTest tests: a benchmark that
# cannot make real requests must never contribute a green test result.  See
# testing/http3/ACCEPTANCE.md and h3_performance_gate.py.

if(CNETMOD_BUILD_TESTS AND CNETMOD_HAS_QUIC)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        add_test(NAME arena_h3_measurement_unit
            COMMAND "${Python3_EXECUTABLE}" -m unittest discover
                -s "${CMAKE_CURRENT_SOURCE_DIR}" -p test_arena_h3_measure.py)
        set_tests_properties(arena_h3_measurement_unit PROPERTIES
            LABELS "http3;benchmark;unit")
    endif()
endif()

set(CNETMOD_BENCH_TARGETS ${BENCH_TARGETS} PARENT_SCOPE)
