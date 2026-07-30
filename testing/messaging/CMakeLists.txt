if(CNETMOD_ENABLE_AMQP091 OR CNETMOD_ENABLE_AMQP10 OR CNETMOD_ENABLE_KAFKA)
    find_package(Python3 COMPONENTS Interpreter QUIET)
endif()

if(NOT Python3_Interpreter_FOUND)
    return()
endif()

if(CNETMOD_ENABLE_AMQP091)
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
cnetmod_link_nlohmann_json(amqp091_interop_driver)
set_property(TARGET amqp091_interop_driver PROPERTY
    CXX_MODULE_GENERATION_MODE "SEPARATE")
endif()

if(CNETMOD_ENABLE_AMQP10)
add_executable(amqp10_interop_driver
    "drivers/amqp10/amqp10_interop_driver_main.cpp"
    "drivers/amqp10/amqp10_interop_driver_runtime.cpp"
)
target_sources(amqp10_interop_driver PRIVATE
    FILE_SET cxx_modules TYPE CXX_MODULES FILES
        "drivers/amqp10/amqp10_interop_driver.cppm"
)
target_link_libraries(amqp10_interop_driver PRIVATE cnetmod_core)
cnetmod_link_nlohmann_json(amqp10_interop_driver)
set_property(TARGET amqp10_interop_driver PROPERTY
    CXX_MODULE_GENERATION_MODE "SEPARATE")
endif()

if(CNETMOD_ENABLE_KAFKA)
add_executable(kafka_interop_driver
    "drivers/kafka/kafka_interoperability_driver_main.cpp"
    "drivers/kafka/kafka_interoperability_driver.cpp"
)
target_sources(kafka_interop_driver PRIVATE
    FILE_SET cxx_modules TYPE CXX_MODULES FILES
        "drivers/kafka/kafka_interoperability_driver.cppm"
)
target_link_libraries(kafka_interop_driver PRIVATE cnetmod_core)
cnetmod_link_nlohmann_json(kafka_interop_driver)
set_property(TARGET kafka_interop_driver PROPERTY
    CXX_MODULE_GENERATION_MODE "SEPARATE")
endif()

function(cnetmod_add_messaging_interoperability_test PROTOCOL TARGET_NAME DRIVER_VARIABLE)
    if(NOT TARGET ${TARGET_NAME})
        return()
    endif()
    add_test(
        NAME python_${PROTOCOL}_interoperability
        COMMAND Python3::Interpreter
            "${CMAKE_CURRENT_SOURCE_DIR}/run_messaging_interoperability.py"
            "${PROTOCOL}"
    )
    set_tests_properties(python_${PROTOCOL}_interoperability PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        ENVIRONMENT "${DRIVER_VARIABLE}=$<TARGET_FILE:${TARGET_NAME}>"
        LABELS "interoperability;messaging;${PROTOCOL}"
        TIMEOUT 900
        SKIP_RETURN_CODE 77
    )
endfunction()

# A partial protocol build collects only its own Python suite and dependencies.
cnetmod_add_messaging_interoperability_test(
    amqp091 amqp091_interop_driver CNETMOD_AMQP091_INTEROP_DRIVER)
cnetmod_add_messaging_interoperability_test(
    amqp10 amqp10_interop_driver CNETMOD_AMQP10_INTEROP_DRIVER)
cnetmod_add_messaging_interoperability_test(
    kafka kafka_interop_driver CNETMOD_KAFKA_INTEROP_DRIVER)
