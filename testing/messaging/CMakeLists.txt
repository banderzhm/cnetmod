find_package(Python3 COMPONENTS Interpreter QUIET)

if(NOT Python3_Interpreter_FOUND)
    return()
endif()

add_executable(amqp091_interop_driver
    "drivers/amqp091/main.cpp"
    "drivers/amqp091/json_lines_driver.cpp"
    "drivers/amqp091/rabbitmq_operation_executor.cpp"
)
target_sources(amqp091_interop_driver PRIVATE
    FILE_SET cxx_modules TYPE CXX_MODULES FILES
        "drivers/amqp091/json_lines_driver.cppm"
        "drivers/amqp091/rabbitmq_operation_executor.cppm"
)
target_link_libraries(amqp091_interop_driver PRIVATE cnetmod_core)
set_property(TARGET amqp091_interop_driver PROPERTY
    CXX_MODULE_GENERATION_MODE "SEPARATE")

add_executable(amqp10_interop_driver
    "drivers/amqp10/amqp10_interop_driver_main.cpp"
    "drivers/amqp10/amqp10_interop_driver_runtime.cpp"
)
target_sources(amqp10_interop_driver PRIVATE
    FILE_SET cxx_modules TYPE CXX_MODULES FILES
        "drivers/amqp10/amqp10_interop_driver.cppm"
)
target_link_libraries(amqp10_interop_driver PRIVATE cnetmod_core)
set_property(TARGET amqp10_interop_driver PROPERTY
    CXX_MODULE_GENERATION_MODE "SEPARATE")

add_executable(kafka_interop_driver
    "drivers/kafka/kafka_interoperability_driver_main.cpp"
    "drivers/kafka/kafka_interoperability_driver.cpp"
)
target_sources(kafka_interop_driver PRIVATE
    FILE_SET cxx_modules TYPE CXX_MODULES FILES
        "drivers/kafka/kafka_interoperability_driver.cppm"
)
target_link_libraries(kafka_interop_driver PRIVATE cnetmod_core)
set_property(TARGET kafka_interop_driver PROPERTY
    CXX_MODULE_GENERATION_MODE "SEPARATE")

# Each protocol implementation owns its driver target.  Registration is
# intentionally conditional so a partial protocol build remains usable.
set(_messaging_driver_environment)
if(TARGET amqp091_interop_driver)
    list(APPEND _messaging_driver_environment
        "CNETMOD_AMQP091_INTEROP_DRIVER=$<TARGET_FILE:amqp091_interop_driver>")
endif()
if(TARGET amqp10_interop_driver)
    list(APPEND _messaging_driver_environment
        "CNETMOD_AMQP10_INTEROP_DRIVER=$<TARGET_FILE:amqp10_interop_driver>")
endif()
if(TARGET kafka_interop_driver)
    list(APPEND _messaging_driver_environment
        "CNETMOD_KAFKA_INTEROP_DRIVER=$<TARGET_FILE:kafka_interop_driver>")
endif()

if(_messaging_driver_environment)
    add_test(
        NAME python_messaging_broker_interoperability
        COMMAND Python3::Interpreter
            "${CMAKE_CURRENT_SOURCE_DIR}/run_messaging_interoperability.py"
    )
    set_tests_properties(python_messaging_broker_interoperability PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        ENVIRONMENT "${_messaging_driver_environment}"
        LABELS "interoperability;messaging"
        TIMEOUT 900
        SKIP_RETURN_CODE 77
    )
endif()
