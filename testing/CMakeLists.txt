# cnetmod testing, benchmarks, and TFB configuration

if(CNETMOD_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    add_subdirectory(tests)
endif()

if(CNETMOD_BUILD_BENCH AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/bench/CMakeLists.txt")
    add_subdirectory(bench)
endif()

if(CNETMOD_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/messaging/CMakeLists.txt")
    add_subdirectory(messaging)
endif()

if(CNETMOD_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/database/CMakeLists.txt")
    add_subdirectory(database)
endif()

if(CNETMOD_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/CMakeLists.txt")
    add_subdirectory(examples)
endif()

if(CNETMOD_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/http3/CMakeLists.txt")
    add_subdirectory(http3)
endif()

option(CNETMOD_BUILD_FUZZERS "Build libFuzzer protocol targets (Clang only)" OFF)
if(CNETMOD_BUILD_FUZZERS AND CNETMOD_HAS_QUIC)
    if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        message(FATAL_ERROR "CNETMOD_BUILD_FUZZERS requires a Clang/libFuzzer toolchain")
    endif()
    add_executable(fuzz_quic_packet fuzz/fuzz_quic_packet.cpp)
    target_link_libraries(fuzz_quic_packet PRIVATE cnetmod_core)
    target_compile_options(fuzz_quic_packet PRIVATE -fsanitize=fuzzer,address,undefined)
    target_link_options(fuzz_quic_packet PRIVATE -fsanitize=fuzzer,address,undefined)
    set_property(TARGET fuzz_quic_packet PROPERTY CXX_MODULE_GENERATION_MODE SEPARATE)
endif()

set(CNETMOD_TEST_TARGETS ${CNETMOD_TEST_TARGETS} PARENT_SCOPE)
set(CNETMOD_BENCH_TARGETS ${CNETMOD_BENCH_TARGETS} PARENT_SCOPE)
