# ==============================================================================
# Mimalloc.cmake
# Automatically detect and use mimalloc allocator if available
# ==============================================================================

# Function: cnetmod_use_mimalloc
# 
# Detects if mimalloc is available and configures the project to use it.
# mimalloc is a high-performance memory allocator from Microsoft that can
# significantly improve performance for multi-threaded applications.
#
# Benefits:
#   - 2-3x faster allocation/deallocation compared to system malloc
#   - Better multi-threaded scalability
#   - Lower memory fragmentation
#   - Especially beneficial for network servers with many concurrent connections
#
# Usage:
#   include(${CMAKE_SOURCE_DIR}/cmake/3rdparty/Mimalloc.cmake)
#   cnetmod_use_mimalloc()
#
# The function will:
#   1. Try to find mimalloc via find_package
#   2. If found, link all targets to mimalloc
#   3. Print status message
#
# Installation:
#   Ubuntu/Debian: sudo apt install libmimalloc-dev
#   Arch Linux: sudo pacman -S mimalloc
#   macOS: brew install mimalloc
#   From source: https://github.com/microsoft/mimalloc
#
function(cnetmod_use_mimalloc)
    # Prefer the active package toolchain. Some IDE integrations install a
    # manifest into <build>/vcpkg_installed but do not append that prefix to
    # CMAKE_PREFIX_PATH, so plain find_package() incorrectly falls back to
    # the system allocator even though mimalloc is present.
    find_package(mimalloc CONFIG QUIET)
    if(NOT mimalloc_FOUND)
        file(GLOB _cnetmod_mimalloc_vcpkg_configs
            "${CMAKE_BINARY_DIR}/vcpkg_installed/*/share/mimalloc/mimalloc-config.cmake")
        list(LENGTH _cnetmod_mimalloc_vcpkg_configs _cnetmod_mimalloc_config_count)
        if(_cnetmod_mimalloc_config_count GREATER 0)
            list(GET _cnetmod_mimalloc_vcpkg_configs 0 _cnetmod_mimalloc_config)
            include("${_cnetmod_mimalloc_config}")
            if(TARGET mimalloc)
                set(mimalloc_FOUND TRUE)
                get_filename_component(mimalloc_DIR "${_cnetmod_mimalloc_config}" DIRECTORY)
            endif()
        endif()
    endif()
    
    if(mimalloc_FOUND)
        message(STATUS "Found mimalloc: ${mimalloc_DIR}")

        # vcpkg's imported mimalloc target provides Debug and Release
        # locations only. Without an explicit mapping, Visual Studio selects
        # mimalloc-debug.dll for RelWithDebInfo; that DLL uses the Debug CRT
        # while cnetmod uses /MD, so mimalloc-redirect initializes too late
        # and allocation interception is disabled. RelWithDebInfo is an
        # optimized /MD configuration and must use the Release DLL.
        if(WIN32 AND TARGET mimalloc)
            set_property(TARGET mimalloc APPEND PROPERTY
                MAP_IMPORTED_CONFIG_RELWITHDEBINFO Release)
            set_property(TARGET mimalloc APPEND PROPERTY
                MAP_IMPORTED_CONFIG_MINSIZEREL Release)
        endif()
        
        # Create an interface library to propagate mimalloc to all targets
        if(NOT TARGET cnetmod::mimalloc)
            add_library(cnetmod::mimalloc INTERFACE IMPORTED GLOBAL)
            target_link_libraries(cnetmod::mimalloc INTERFACE mimalloc)
        endif()
        
        # Set cache variable to indicate mimalloc is being used
        set(CNETMOD_USING_MIMALLOC TRUE CACHE BOOL "Using mimalloc allocator" FORCE)
        set(CNETMOD_MIMALLOC_TARGET "mimalloc" CACHE STRING "mimalloc target name" FORCE)
        
        message(STATUS "Configured to use mimalloc allocator")
        
    else()
        # Try to find mimalloc library directly (fallback for systems without CMake config)
        find_library(MIMALLOC_LIBRARY NAMES mimalloc)
        find_path(MIMALLOC_INCLUDE_DIR NAMES mimalloc.h)
        
        if(MIMALLOC_LIBRARY AND MIMALLOC_INCLUDE_DIR)
            message(STATUS "Found mimalloc library: ${MIMALLOC_LIBRARY}")
            message(STATUS "Found mimalloc headers: ${MIMALLOC_INCLUDE_DIR}")
            
            # Create an interface library
            if(NOT TARGET cnetmod::mimalloc)
                add_library(cnetmod::mimalloc INTERFACE IMPORTED GLOBAL)
                target_link_libraries(cnetmod::mimalloc INTERFACE ${MIMALLOC_LIBRARY})
                target_include_directories(cnetmod::mimalloc INTERFACE ${MIMALLOC_INCLUDE_DIR})
            endif()
            
            set(CNETMOD_USING_MIMALLOC TRUE CACHE BOOL "Using mimalloc allocator" FORCE)
            set(CNETMOD_MIMALLOC_TARGET "cnetmod::mimalloc" CACHE STRING "mimalloc target name" FORCE)
            
            message(STATUS "Configured to use mimalloc allocator (via direct library)")
            
        else()
            message(STATUS "mimalloc not found, using system allocator")
            message(STATUS "  Install mimalloc for better performance:")
            if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
                message(STATUS "    Ubuntu/Debian: sudo apt install libmimalloc-dev")
                message(STATUS "    Arch Linux: sudo pacman -S mimalloc")
            elseif(APPLE)
                message(STATUS "    macOS: brew install mimalloc")
            elseif(WIN32)
                message(STATUS "    Windows: vcpkg install mimalloc")
            endif()
            message(STATUS "    From source: https://github.com/microsoft/mimalloc")
            
            set(CNETMOD_USING_MIMALLOC FALSE CACHE BOOL "Using mimalloc allocator" FORCE)
        endif()
    endif()
endfunction()

# Function: cnetmod_link_mimalloc
#
# Links a target to mimalloc if it's available
#
# Parameters:
#   target - The target to link mimalloc to
#
# Usage:
#   cnetmod_link_mimalloc(my_executable)
#
function(cnetmod_link_mimalloc target)
    if(CNETMOD_USING_MIMALLOC)
        if(TARGET mimalloc)
            # cnetmod_core is static. Its allocator dependency must reach
            # final executables; PRIVATE linkage would otherwise leave the
            # import library out of a consumer's link step.
            target_link_libraries(${target} PUBLIC mimalloc)
        elseif(TARGET cnetmod::mimalloc)
            target_link_libraries(${target} PUBLIC cnetmod::mimalloc)
        endif()

    endif()
endfunction()

# Dynamic mimalloc on Windows uses a redirect DLL beside the executable.  CMake
# propagates the import library through a static cnetmod_core target, but does
# not copy imported runtime DLLs for Visual Studio projects automatically.
# Attach this to executable targets so command-line runs work as well as IDE
# launches; copying both files is required for CRT allocation redirection.
function(cnetmod_deploy_mimalloc_runtime target)
    if(NOT (WIN32 AND CNETMOD_USING_MIMALLOC AND TARGET mimalloc))
        return()
    endif()

    get_target_property(_cnetmod_mimalloc_type mimalloc TYPE)
    if(NOT _cnetmod_mimalloc_type STREQUAL "SHARED_LIBRARY")
        return()
    endif()

    # mimalloc's Windows redirect DLL activates only when the main executable
    # imports a mimalloc API. Keep that requirement on the executable which
    # also receives the DLLs; a static cnetmod_core must not make unrelated
    # tests and applications unloadable.
    if(MSVC)
        target_link_options(${target} PRIVATE "LINKER:/INCLUDE:mi_version")
    endif()

    add_custom_command(TARGET ${target} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:mimalloc>"
            "$<TARGET_FILE_DIR:${target}>"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE_DIR:mimalloc>/mimalloc-redirect.dll"
            "$<TARGET_FILE_DIR:${target}>"
        VERBATIM)
endfunction()

# Function: cnetmod_print_allocator_info
#
# Prints information about the memory allocator being used
#
function(cnetmod_print_allocator_info)
    if(CNETMOD_USING_MIMALLOC)
        message(STATUS "===========================================")
        message(STATUS "Memory Allocator: mimalloc (high-performance)")
        message(STATUS "Expected performance improvement: 2-3x")
        message(STATUS "===========================================")
    else()
        message(STATUS "===========================================")
        message(STATUS "Memory Allocator: system default")
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            message(STATUS "Tip: Install mimalloc for better performance")
            message(STATUS "  Ubuntu/Debian: sudo apt install libmimalloc-dev")
            message(STATUS "  Arch Linux: sudo pacman -S mimalloc")
        elseif(APPLE)
            message(STATUS "Tip: Install mimalloc for better performance")
            message(STATUS "  macOS: brew install mimalloc")
        elseif(WIN32)
            message(STATUS "Tip: Install mimalloc for better performance")
            message(STATUS "  Windows: vcpkg install mimalloc")
        endif()
        message(STATUS "===========================================")
    endif()
endfunction()
