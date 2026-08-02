
####### Expanded from @PACKAGE_INIT@ by configure_package_config_file() #######
####### Any changes to this file will be overwritten by the next CMake run ####
####### The input file was cnetmodConfig.cmake.in                            ########

get_filename_component(PACKAGE_PREFIX_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../" ABSOLUTE)

macro(set_and_check _var _file)
  set(${_var} "${_file}")
  if(NOT EXISTS "${_file}")
    message(FATAL_ERROR "File or directory ${_file} referenced by variable ${_var} does not exist !")
  endif()
endmacro()

macro(check_required_components _NAME)
  foreach(comp ${${_NAME}_FIND_COMPONENTS})
    if(NOT ${_NAME}_${comp}_FOUND)
      if(${_NAME}_FIND_REQUIRED_${comp})
        set(${_NAME}_FOUND FALSE)
      endif()
    endif()
  endforeach()
endmacro()

####################################################################################

include(CMakeFindDependencyMacro)

# leveldb is exported as part of the package when enabled.  Its public link
# interface refers to Threads::Threads, so create that imported target before
# loading cnetmodTargets.cmake in every consumer project.
if(ON)
    find_dependency(Threads)
endif()

# Resolve exactly the dependencies retained by the configured static target.
if(ON)
    find_dependency(OpenSSL)
endif()
if(ON)
    find_dependency(ZLIB)
endif()
if(ON)
    find_dependency(ICU COMPONENTS uc i18n)
endif()
if(ON)
    if("pugixml-static" STREQUAL "pugixml-static")
        # The bundled static archive is installed with cnetmod, but it is not
        # part of cnetmodTargets. Recreate the link target for consumers.
        if(NOT TARGET pugixml::static)
            add_library(pugixml::static STATIC IMPORTED)
            set_target_properties(pugixml::static PROPERTIES
                IMPORTED_LOCATION "${PACKAGE_PREFIX_DIR}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}pugixml${CMAKE_STATIC_LIBRARY_SUFFIX}"
                INTERFACE_INCLUDE_DIRECTORIES "${PACKAGE_PREFIX_DIR}/include")
        endif()
    else()
        find_dependency(pugixml CONFIG)
    endif()
endif()

if(ON)
    find_package(lz4 CONFIG QUIET)
    if(NOT TARGET lz4::lz4 AND NOT TARGET LZ4::lz4 AND
       NOT TARGET LZ4::LZ4 AND NOT TARGET unofficial::lz4::lz4 AND
       NOT TARGET lz4_static AND NOT TARGET lz4_shared)
        find_package(LZ4 CONFIG QUIET)
    endif()
endif()

include("${CMAKE_CURRENT_LIST_DIR}/cnetmodTargets.cmake")

# MSBuild supplies `import std` itself, but Ninja has no equivalent implicit
# provider.  Add the MSVC STL module units to the imported module graph when a
# consumer uses Ninja.  This keeps the release archive binary-only while still
# allowing downstream `.cppm` units to import `std`.
if(MSVC AND CMAKE_GENERATOR MATCHES "Ninja")
    get_filename_component(_cnetmod_compiler_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    get_filename_component(_cnetmod_compiler_host "${_cnetmod_compiler_bin}" DIRECTORY)
    get_filename_component(_cnetmod_compiler_bin_root "${_cnetmod_compiler_host}" DIRECTORY)
    get_filename_component(_cnetmod_msvc_root "${_cnetmod_compiler_bin_root}" DIRECTORY)
    set(_cnetmod_msvc_modules "${_cnetmod_msvc_root}/modules")

    if(EXISTS "${_cnetmod_msvc_modules}/std.ixx" AND
       EXISTS "${_cnetmod_msvc_modules}/std.compat.ixx")
        target_sources(cnetmod::cnetmod_core INTERFACE
            FILE_SET cxx_std_modules TYPE CXX_MODULES
            BASE_DIRS "${_cnetmod_msvc_modules}"
            FILES
                "${_cnetmod_msvc_modules}/std.ixx"
                "${_cnetmod_msvc_modules}/std.compat.ixx")
    else()
        message(FATAL_ERROR
            "cnetmod requires MSVC STL module units for Ninja consumers. "
            "Set up a complete MSVC toolchain that provides std.ixx.")
    endif()
endif()

check_required_components(cnetmod)
