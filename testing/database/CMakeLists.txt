if(CNETMOD_ENABLE_POSTGRESQL OR CNETMOD_ENABLE_MONGODB)
    find_package(Python3 COMPONENTS Interpreter QUIET)
endif()

if(CNETMOD_ENABLE_POSTGRESQL)
    add_executable(postgresql_interoperability_driver
        drivers/postgresql/postgresql_interoperability_driver_main.cpp
        drivers/postgresql/postgresql_interoperability_driver.cpp
    )
    target_sources(postgresql_interoperability_driver PRIVATE
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
            drivers/postgresql/postgresql_interoperability_driver.cppm
    )
    target_link_libraries(postgresql_interoperability_driver PRIVATE cnetmod_core)
    set_property(TARGET postgresql_interoperability_driver PROPERTY
        CXX_MODULE_GENERATION_MODE "SEPARATE")
    list(APPEND CNETMOD_TEST_TARGETS postgresql_interoperability_driver)
endif()

if(CNETMOD_ENABLE_MONGODB)
    add_executable(mongodb_interoperability_driver
        drivers/mongodb/mongodb_interoperability_driver_main.cpp
        drivers/mongodb/mongodb_interoperability_driver.cpp
    )
    target_sources(mongodb_interoperability_driver PRIVATE
        FILE_SET cxx_modules TYPE CXX_MODULES FILES
            drivers/mongodb/mongodb_interoperability_driver.cppm
    )
    target_link_libraries(mongodb_interoperability_driver PRIVATE cnetmod_core)
    set_property(TARGET mongodb_interoperability_driver PROPERTY
        CXX_MODULE_GENERATION_MODE "SEPARATE")
    list(APPEND CNETMOD_TEST_TARGETS mongodb_interoperability_driver)
endif()

if(Python3_Interpreter_FOUND AND TARGET postgresql_interoperability_driver)
    add_test(
        NAME python_postgresql_interoperability
        COMMAND Python3::Interpreter
            "${CMAKE_CURRENT_SOURCE_DIR}/run_optional_pytest.py"
            --config pytest.ini --module psycopg postgresql
    )
    set_tests_properties(python_postgresql_interoperability PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        ENVIRONMENT "CNETMOD_POSTGRESQL_DRIVER=$<TARGET_FILE:postgresql_interoperability_driver>"
        LABELS "interoperability;database;postgresql"
        SKIP_RETURN_CODE 77
        TIMEOUT 900
    )
endif()

if(Python3_Interpreter_FOUND AND TARGET mongodb_interoperability_driver)
    add_test(
        NAME python_mongodb_interoperability
        COMMAND Python3::Interpreter
            "${CMAKE_CURRENT_SOURCE_DIR}/run_optional_pytest.py"
            --config pytest.ini --module pymongo mongodb
    )
    set_tests_properties(python_mongodb_interoperability PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        ENVIRONMENT "CNETMOD_MONGODB_DRIVER=$<TARGET_FILE:mongodb_interoperability_driver>"
        LABELS "interoperability;database;mongodb"
        SKIP_RETURN_CODE 77
        TIMEOUT 900
    )
endif()

set(CNETMOD_TEST_TARGETS ${CNETMOD_TEST_TARGETS} PARENT_SCOPE)

# A real, isolated ClickHouse server is used when the local binary is present.
# The runner owns its temporary data, loopback ports and process cleanup.
if(CNETMOD_ENABLE_CLICKHOUSE)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    find_program(CNETMOD_CLICKHOUSE_EXECUTABLE NAMES clickhouse
        HINTS "$ENV{HOME}/.local/bin")
    find_program(CNETMOD_CLICKHOUSE_OPENSSL_EXECUTABLE NAMES openssl)
    if(Python3_Interpreter_FOUND AND CNETMOD_CLICKHOUSE_EXECUTABLE
            AND CNETMOD_CLICKHOUSE_OPENSSL_EXECUTABLE)
        set(_clickhouse_application_args)
        if(TARGET test_application_clickhouse)
            list(APPEND _clickhouse_application_args --application-executable "$<TARGET_FILE:test_application_clickhouse>")
        endif()
        add_test(NAME python_clickhouse_interoperability
            COMMAND Python3::Interpreter
                "${CMAKE_CURRENT_SOURCE_DIR}/run_clickhouse_native.py"
                --clickhouse "${CNETMOD_CLICKHOUSE_EXECUTABLE}"
                --openssl "${CNETMOD_CLICKHOUSE_OPENSSL_EXECUTABLE}"
                --test-executable "$<TARGET_FILE:test_clickhouse_live>" ${_clickhouse_application_args})
        set_tests_properties(python_clickhouse_interoperability PROPERTIES
            TIMEOUT 300 LABELS "interoperability;database;clickhouse")
    endif()
endif()
