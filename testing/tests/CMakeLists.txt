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
    test_log.cpp
    test_http_types.cpp
    test_http_request.cpp
    test_http_response.cpp
    test_http_parser.cpp
    test_http_router.cpp
    test_http_utils.cpp
    test_tfb_compliance.cpp
)

foreach(test_src ${TEST_SOURCES})
    # Extract test name from filename (e.g. test_task.cpp → test_task)
    get_filename_component(test_name ${test_src} NAME_WE)

    add_executable(${test_name} ${test_src})
    target_link_libraries(${test_name} PRIVATE cnetmod_core)
    target_include_directories(${test_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
    set_property(TARGET ${test_name} PROPERTY
        CXX_MODULE_GENERATION_MODE "SEPARATE"
    )

    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
