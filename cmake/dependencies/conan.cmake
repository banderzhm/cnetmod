include_guard(GLOBAL)

get_filename_component(_cnetmod_toolchain_name "${CMAKE_TOOLCHAIN_FILE}" NAME)
if(NOT _cnetmod_toolchain_name MATCHES "^conan_toolchain(\\.cmake)?$")
    message(FATAL_ERROR
        "Conan mode requires Conan 2 CMakeToolchain (-DCMAKE_TOOLCHAIN_FILE=<generated>/conan_toolchain.cmake)")
endif()

include(${CMAKE_CURRENT_LIST_DIR}/PackageDependencies.cmake)

macro(cnetmod_configure_provider_dependencies)
    cnetmod_configure_package_dependencies()
endmacro()
