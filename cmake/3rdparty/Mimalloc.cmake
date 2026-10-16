# Configure the mimalloc source pinned in 3rdparty/mimalloc.
#
# Allocator replacement belongs to the final executable. This file prepares
# the bundled implementation; cnetmodMimalloc.cmake exposes the public helper
# used by cnetmod and by installed-SDK consumers.

function(cnetmod_use_mimalloc)
    set(_cnetmod_mimalloc_source "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/mimalloc")
    if(NOT EXISTS "${_cnetmod_mimalloc_source}/CMakeLists.txt")
        message(FATAL_ERROR
            "CNETMOD_USE_MIMALLOC=ON requires 3rdparty/mimalloc. "
            "Run: git submodule update --init 3rdparty/mimalloc")
    endif()

    # cnetmod is a C++-only project. Build mimalloc with the active C++
    # toolchain so embedding it does not require enabling a second compiler.
    set(MI_USE_CXX ON CACHE STRING "Build bundled mimalloc with C++" FORCE)
    set(MI_OVERRIDE ON CACHE BOOL "Override the process allocator" FORCE)
    # The upstream defaults keep allocation statistics and sampled profiling
    # enabled even in Release builds. Both add work to every hot allocation,
    # which is the opposite of what an opt-in production allocator should do.
    # Diagnostics belong in an explicitly configured mimalloc build instead.
    set(MI_DEBUG OFF CACHE STRING "Disable mimalloc assertions" FORCE)
    set(MI_GUARDED OFF CACHE STRING "Disable sampled guard pages" FORCE)
    set(MI_STATS OFF CACHE STRING "Disable allocation statistics" FORCE)
    set(MI_PROFILE OFF CACHE STRING "Disable allocation profiling" FORCE)
    set(MI_BUILD_TESTS OFF CACHE BOOL "Do not build mimalloc tests" FORCE)
    set(MI_INSTALL_TOPLEVEL ON CACHE BOOL "Install mimalloc in the cnetmod SDK" FORCE)

    if(WIN32)
        # Windows allocator replacement requires the DLL and redirect DLL.
        set(MI_BUILD_SHARED ON CACHE BOOL "Build the mimalloc runtime DLL" FORCE)
        set(MI_BUILD_STATIC OFF CACHE BOOL "Do not build unused static mimalloc" FORCE)
        set(MI_BUILD_OBJECT OFF CACHE BOOL "Do not build unused mimalloc object" FORCE)
        set(MI_WIN_REDIRECT ON CACHE BOOL "Enable Windows CRT redirection" FORCE)
    else()
        # Unix linkers can discard allocator symbols from an archive. The
        # upstream-recommended single object gives deterministic process-wide
        # malloc/new replacement when attached to the final executable.
        set(MI_BUILD_SHARED OFF CACHE BOOL "Do not build unused shared mimalloc" FORCE)
        set(MI_BUILD_STATIC ON CACHE BOOL "Build mimalloc object dependency" FORCE)
        set(MI_BUILD_OBJECT ON CACHE BOOL "Build mimalloc override object" FORCE)
    endif()

    add_subdirectory("${_cnetmod_mimalloc_source}"
        "${CMAKE_CURRENT_BINARY_DIR}/3rdparty/mimalloc")

    set(CNETMOD_USING_MIMALLOC TRUE CACHE BOOL "Using bundled mimalloc" FORCE)
    set(CNETMOD_MIMALLOC_BUNDLED TRUE CACHE INTERNAL "mimalloc is bundled" FORCE)

    if(WIN32)
        set(CNETMOD_MIMALLOC_TARGET mimalloc CACHE INTERNAL "mimalloc runtime target" FORCE)
        set(CNETMOD_MIMALLOC_REDIRECT
            "$<TARGET_FILE_DIR:mimalloc>/mimalloc-redirect${MIMALLOC_REDIRECT_SUFFIX}.dll"
            CACHE INTERNAL "mimalloc redirect DLL" FORCE)
    else()
        set(CNETMOD_MIMALLOC_OBJECT "$<TARGET_OBJECTS:mimalloc-obj>"
            CACHE INTERNAL "mimalloc override object" FORCE)
    endif()

    include("${CMAKE_CURRENT_FUNCTION_LIST_DIR}/../cnetmodMimalloc.cmake")
    message(STATUS "Memory allocator: bundled mimalloc 3.5.3")
endfunction()

# Compatibility wrappers for existing in-tree CMake files. Both now perform
# the correct final-executable operation rather than linking a static library.
function(cnetmod_link_mimalloc target)
    get_target_property(_cnetmod_target_type ${target} TYPE)
    if(_cnetmod_target_type STREQUAL "EXECUTABLE")
        cnetmod_enable_mimalloc(${target})
    endif()
endfunction()

function(cnetmod_deploy_mimalloc_runtime target)
    if(CNETMOD_USING_MIMALLOC)
        cnetmod_enable_mimalloc(${target})
    endif()
endfunction()

function(cnetmod_print_allocator_info)
    if(CNETMOD_USING_MIMALLOC)
        message(STATUS "Memory allocator: bundled mimalloc (enabled per executable)")
    else()
        message(STATUS "Memory allocator: system default")
    endif()
endfunction()
