include_guard(GLOBAL)

include(FetchContent)

#[[
  Makes the yaml-cpp C++23 module facade available to Application.

  The facade is deliberately a composition dependency rather than a core
  dependency: only the Application YAML document adapter imports it.  A local
  source directory is useful while developing the facade; ordinary consumers
  fetch the reviewed, immutable commit from its public repository.
]]
macro(cnetmod_configure_yaml_cpp_modules)
    option(CNETMOD_ENABLE_YAML_CONFIGURATION
        "Enable YAML files for cnetmod.application configuration" ON)
    set(CNETMOD_YAML_CPP_MODULES_SOURCE_DIR "" CACHE PATH
        "Local yaml-cpp-modules source directory (development override)")
    set(CNETMOD_YAML_CPP_MODULES_REVISION
        "aaa6f47aea74e3e25fc2494ae76b0d422ad4f03b" CACHE STRING
        "Immutable yaml-cpp-modules Git revision")

    if(NOT CNETMOD_ENABLE_YAML_CONFIGURATION OR NOT CNETMOD_ENABLE_HTTP)
        set(CNETMOD_HAS_YAML_CONFIGURATION OFF)
        return()
    endif()

    if(CNETMOD_YAML_CPP_MODULES_SOURCE_DIR)
        if(NOT EXISTS "${CNETMOD_YAML_CPP_MODULES_SOURCE_DIR}/CMakeLists.txt")
            message(FATAL_ERROR
                "CNETMOD_YAML_CPP_MODULES_SOURCE_DIR must contain CMakeLists.txt")
        endif()
        set(YAML_CPP_MODULES_BUILD_TESTS OFF CACHE BOOL
            "Build yaml-cpp module smoke tests" FORCE)
        set(YAML_CPP_MODULES_INSTALL ON CACHE BOOL
            "Install yaml-cpp module facade targets" FORCE)
        add_subdirectory("${CNETMOD_YAML_CPP_MODULES_SOURCE_DIR}"
            "${CMAKE_BINARY_DIR}/_deps/yaml-cpp-modules-build" EXCLUDE_FROM_ALL)
    else()
        FetchContent_Declare(yaml_cpp_modules
            GIT_REPOSITORY https://github.com/banderzhm/yaml-cpp-modules.git
            GIT_TAG ${CNETMOD_YAML_CPP_MODULES_REVISION}
            EXCLUDE_FROM_ALL)
        set(YAML_CPP_MODULES_BUILD_TESTS OFF CACHE BOOL
            "Build yaml-cpp module smoke tests" FORCE)
        set(YAML_CPP_MODULES_INSTALL ON CACHE BOOL
            "Install yaml-cpp module facade targets" FORCE)
        FetchContent_MakeAvailable(yaml_cpp_modules)
    endif()

    if(NOT TARGET yaml_cpp::yaml_cpp)
        message(FATAL_ERROR "yaml-cpp-modules did not provide yaml_cpp::yaml_cpp")
    endif()
    set(CNETMOD_HAS_YAML_CONFIGURATION ON)
endmacro()

function(cnetmod_link_yaml_cpp_modules TARGET_NAME)
    if(CNETMOD_HAS_YAML_CONFIGURATION)
        target_link_libraries(${TARGET_NAME} PRIVATE yaml_cpp::yaml_cpp)
    endif()
endfunction()
