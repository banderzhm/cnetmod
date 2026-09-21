find_package(Python3 COMPONENTS Interpreter QUIET)

if(NOT Python3_Interpreter_FOUND OR NOT CNETMOD_BUILD_EXAMPLES)
    return()
endif()

# Keep protocol suites separate: each test imports only its own reference client
# and is registered only when the corresponding production example can exist.
# Register an end-to-end test only when the corresponding example target is
# part of this build. Feature switches alone do not guarantee that an optional
# example is enabled; keeping the target check here prevents a dangling
# TARGET_FILE generator expression during CMake generation.
if(TARGET example_postgresql_production_service)
    add_test(
        NAME python_postgresql_production_example_e2e
        COMMAND Python3::Interpreter
            "${CMAKE_SOURCE_DIR}/testing/database/run_optional_pytest.py"
            --config pytest.ini --module psycopg database/postgresql
    )
    set_tests_properties(python_postgresql_production_example_e2e PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        ENVIRONMENT "CNETMOD_POSTGRESQL_EXAMPLE=$<TARGET_FILE:example_postgresql_production_service>"
        LABELS "e2e;examples;database;postgresql"
        SKIP_RETURN_CODE 77
        TIMEOUT 900
    )
endif()

if(CNETMOD_ENABLE_MONGODB)
    add_test(
        NAME python_mongodb_production_example_e2e
        COMMAND Python3::Interpreter
            "${CMAKE_SOURCE_DIR}/testing/database/run_optional_pytest.py"
            --config pytest.ini --module pymongo database/mongodb
    )
    set_tests_properties(python_mongodb_production_example_e2e PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        ENVIRONMENT "CNETMOD_MONGODB_EXAMPLE=$<TARGET_FILE:example_mongodb_production_service>"
        LABELS "e2e;examples;database;mongodb"
        SKIP_RETURN_CODE 77
        TIMEOUT 1200
    )
endif()
