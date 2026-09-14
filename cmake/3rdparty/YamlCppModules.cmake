include_guard(GLOBAL)

#[[
  Makes the yaml-cpp C++23 module facade available to Application.

  The facade is deliberately a composition dependency rather than a core
  dependency: only the Application YAML document adapter imports it.  A local
  source directory is useful while developing the facade; ordinary builds use
  the repository's pinned 3rdparty/yaml-cpp-modules submodule.
]]
macro(cnetmod_configure_yaml_cpp_modules)
    option(CNETMOD_ENABLE_YAML_CONFIGURATION
        "Enable YAML files for cnetmod.application configuration" ON)
    set(CNETMOD_YAML_CPP_MODULES_SOURCE_DIR "" CACHE PATH
        "Local yaml-cpp-modules source directory (development override)")

    if(NOT CNETMOD_ENABLE_YAML_CONFIGURATION OR NOT CNETMOD_ENABLE_HTTP)
        set(CNETMOD_HAS_YAML_CONFIGURATION OFF)
    else()
        if(CNETMOD_YAML_CPP_MODULES_SOURCE_DIR)
            set(_cnetmod_yaml_cpp_modules_source
                "${CNETMOD_YAML_CPP_MODULES_SOURCE_DIR}")
        else()
            set(_cnetmod_yaml_cpp_modules_source
                "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/yaml-cpp-modules")
        endif()
        if(NOT EXISTS "${_cnetmod_yaml_cpp_modules_source}/CMakeLists.txt")
            message(FATAL_ERROR
                "yaml-cpp-modules is unavailable at ${_cnetmod_yaml_cpp_modules_source}. "
                "Initialize submodules with: git submodule update --init --recursive")
        endif()
        set(YAML_CPP_MODULES_BUILD_TESTS OFF CACHE BOOL
            "Build yaml-cpp module smoke tests" FORCE)
        set(YAML_CPP_MODULES_INSTALL ON CACHE BOOL
            "Install yaml-cpp module facade targets" FORCE)
        add_subdirectory("${_cnetmod_yaml_cpp_modules_source}"
            "${CMAKE_BINARY_DIR}/_deps/yaml-cpp-modules-build")

        if(NOT TARGET yaml_cpp::yaml_cpp)
            message(FATAL_ERROR "yaml-cpp-modules did not provide yaml_cpp::yaml_cpp")
        endif()
        set(CNETMOD_HAS_YAML_CONFIGURATION ON)
    endif()
endmacro()

function(cnetmod_link_yaml_cpp_modules TARGET_NAME)
    if(CNETMOD_HAS_YAML_CONFIGURATION)
        target_link_libraries(${TARGET_NAME} PRIVATE yaml_cpp::yaml_cpp)
    endif()
endfunction()
