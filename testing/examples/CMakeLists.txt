find_package(Python3 COMPONENTS Interpreter QUIET)

if(NOT Python3_Interpreter_FOUND OR NOT CNETMOD_BUILD_EXAMPLES)
    return()
endif()

# Keep protocol suites separate: each test imports only its own reference client
# and is registered only when the corresponding production example can exist.
if(CNETMOD_ENABLE_POSTGRESQL AND CNETMOD_ENABLE_HTTP AND CNETMOD_ENABLE_ORM)
    add_test(
        NAME python_postgresql_production_example_e2e
        COMMAND Python3::Interpreter -m pytest -c pytest.ini database/postgresql
    )
    set_tests_properties(python_postgresql_production_example_e2e PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        ENVIRONMENT "CNETMOD_POSTGRESQL_EXAMPLE=$<TARGET_FILE:example_postgresql_production_service>"
        LABELS "e2e;examples;database;postgresql"
        TIMEOUT 900
    )
endif()

if(CNETMOD_ENABLE_MONGODB)
    add_test(
        NAME python_mongodb_production_example_e2e
        COMMAND Python3::Interpreter -m pytest -c pytest.ini database/mongodb
    )
    set_tests_properties(python_mongodb_production_example_e2e PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        ENVIRONMENT "CNETMOD_MONGODB_EXAMPLE=$<TARGET_FILE:example_mongodb_production_service>"
        LABELS "e2e;examples;database;mongodb"
        TIMEOUT 1200
    )
endif()
