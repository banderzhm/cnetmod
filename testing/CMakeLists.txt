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

set(CNETMOD_TEST_TARGETS ${CNETMOD_TEST_TARGETS} PARENT_SCOPE)
set(CNETMOD_BENCH_TARGETS ${CNETMOD_BENCH_TARGETS} PARENT_SCOPE)
