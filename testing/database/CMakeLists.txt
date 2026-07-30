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
    cnetmod_link_nlohmann_json(postgresql_interoperability_driver)
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
    cnetmod_link_nlohmann_json(mongodb_interoperability_driver)
    set_property(TARGET mongodb_interoperability_driver PROPERTY
        CXX_MODULE_GENERATION_MODE "SEPARATE")
    list(APPEND CNETMOD_TEST_TARGETS mongodb_interoperability_driver)
endif()

if(Python3_Interpreter_FOUND AND TARGET postgresql_interoperability_driver)
    add_test(
        NAME python_postgresql_interoperability
        COMMAND Python3::Interpreter -m pytest -c pytest.ini postgresql
    )
    set_tests_properties(python_postgresql_interoperability PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        ENVIRONMENT "CNETMOD_POSTGRESQL_DRIVER=$<TARGET_FILE:postgresql_interoperability_driver>"
        LABELS "interoperability;database;postgresql"
        TIMEOUT 900
    )
endif()

if(Python3_Interpreter_FOUND AND TARGET mongodb_interoperability_driver)
    add_test(
        NAME python_mongodb_interoperability
        COMMAND Python3::Interpreter -m pytest -c pytest.ini mongodb
    )
    set_tests_properties(python_mongodb_interoperability PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        ENVIRONMENT "CNETMOD_MONGODB_DRIVER=$<TARGET_FILE:mongodb_interoperability_driver>"
        LABELS "interoperability;database;mongodb"
        TIMEOUT 900
    )
endif()

set(CNETMOD_TEST_TARGETS ${CNETMOD_TEST_TARGETS} PARENT_SCOPE)
