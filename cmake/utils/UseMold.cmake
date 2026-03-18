# ==============================================================================
# UseMold.cmake
# Automatically detect and use mold linker on Linux if available
# ==============================================================================

# Function: cnetmod_use_mold
# 
# Detects if mold linker is available and configures the project to use it.
# mold is a modern, high-performance linker that can significantly speed up
# link times for C++ projects.
#
# Benefits:
#   - 10-20x faster linking compared to GNU ld
#   - 3-5x faster than lld
#   - Especially beneficial for large C++23 module projects
#
# Usage:
#   include(${CMAKE_SOURCE_DIR}/cmake/utils/UseMold.cmake)
#   cnetmod_use_mold()
#
# The function will:
#   1. Check if the platform is Linux
#   2. Check if mold is installed and in PATH
#   3. Configure CMake to use mold for linking
#   4. Print status message
#
function(cnetmod_use_mold)
    # Only applicable on Linux
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        return()
    endif()

    # Check if mold is available
    find_program(MOLD_EXECUTABLE mold)
    
    if(MOLD_EXECUTABLE)
        message(STATUS "Found mold linker: ${MOLD_EXECUTABLE}")
        
        # Check compiler type
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            # Clang: use -fuse-ld=mold
            add_link_options(-fuse-ld=mold)
            message(STATUS "Configured to use mold linker with Clang")
            
        elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
            # GCC: use -fuse-ld=mold (GCC 12.1.0+)
            if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL "12.1.0")
                add_link_options(-fuse-ld=mold)
                message(STATUS "Configured to use mold linker with GCC ${CMAKE_CXX_COMPILER_VERSION}")
            else()
                # Older GCC: use -B flag to prepend mold to linker search path
                get_filename_component(MOLD_DIR ${MOLD_EXECUTABLE} DIRECTORY)
                add_link_options(-B${MOLD_DIR})
                message(STATUS "Configured to use mold linker with GCC ${CMAKE_CXX_COMPILER_VERSION} (via -B flag)")
            endif()
        else()
            message(STATUS "mold found but compiler ${CMAKE_CXX_COMPILER_ID} may not support it")
        endif()
        
        # Set a cache variable to indicate mold is being used
        set(CNETMOD_USING_MOLD TRUE CACHE BOOL "Using mold linker" FORCE)
        
    else()
        message(STATUS "mold linker not found, using default linker")
        message(STATUS "  Install mold for faster linking: https://github.com/rui314/mold")
        set(CNETMOD_USING_MOLD FALSE CACHE BOOL "Using mold linker" FORCE)
    endif()
endfunction()

# Function: cnetmod_print_linker_info
#
# Prints information about the linker being used
#
function(cnetmod_print_linker_info)
    if(CNETMOD_USING_MOLD)
        message(STATUS "===========================================")
        message(STATUS "Linker: mold (high-performance)")
        message(STATUS "Expected link time improvement: 10-20x")
        message(STATUS "===========================================")
    else()
        message(STATUS "===========================================")
        message(STATUS "Linker: system default")
        if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
            message(STATUS "Tip: Install mold for faster builds")
            message(STATUS "  Ubuntu/Debian: sudo apt install mold")
            message(STATUS "  Arch Linux: sudo pacman -S mold")
            message(STATUS "  From source: https://github.com/rui314/mold")
        endif()
        message(STATUS "===========================================")
    endif()
endfunction()
