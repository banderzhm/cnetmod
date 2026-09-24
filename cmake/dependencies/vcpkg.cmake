include_guard(GLOBAL)

get_filename_component(_cnetmod_toolchain_name "${CMAKE_TOOLCHAIN_FILE}" NAME)
if(NOT _cnetmod_toolchain_name MATCHES "^vcpkg(\\.cmake)?$")
    message(FATAL_ERROR
        "vcpkg mode requires -DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake")
endif()

include(${CMAKE_CURRENT_LIST_DIR}/PackageDependencies.cmake)

macro(cnetmod_configure_provider_dependencies)
    cnetmod_configure_package_dependencies()
endmacro()
