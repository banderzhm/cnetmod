# ==============================================================================
# UseMimalloc.cmake
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
#   include(${CMAKE_SOURCE_DIR}/cmake/utils/UseMimalloc.cmake)
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
    # Try to find mimalloc package
    find_package(mimalloc QUIET)
    
    if(mimalloc_FOUND)
        message(STATUS "Found mimalloc: ${mimalloc_DIR}")
        
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
            target_link_libraries(${target} PRIVATE mimalloc)
        elseif(TARGET cnetmod::mimalloc)
            target_link_libraries(${target} PRIVATE cnetmod::mimalloc)
        endif()
    endif()
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
