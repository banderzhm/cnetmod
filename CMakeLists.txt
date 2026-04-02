cmake_minimum_required(VERSION 3.28)

project(cnetmod
    VERSION 0.1.0
    DESCRIPTION "Cross-platform asynchronous network library with C++23 modules"
    LANGUAGES CXX
)

# ==============================================================================
# C++ Standard Configuration
# ==============================================================================
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ==============================================================================
# Linker Optimization: Use mold if available (Linux only)
# ==============================================================================
option(CNETMOD_USE_MOLD "Use mold linker for faster linking (Linux only)" ON)
if(CNETMOD_USE_MOLD)
    include(${CMAKE_SOURCE_DIR}/cmake/utils/UseMold.cmake)
    cnetmod_use_mold()
endif()

# ==============================================================================
# Memory Allocator Optimization: Use mimalloc if available
# ==============================================================================
option(CNETMOD_USE_MIMALLOC "Use mimalloc allocator for better performance" ON)
if(CNETMOD_USE_MIMALLOC)
    include(${CMAKE_SOURCE_DIR}/cmake/utils/UseMimalloc.cmake)
    cnetmod_use_mimalloc()
endif()

# ==============================================================================
# Auto-detect platform-specific standard library module paths
# ==============================================================================
#[[
  Load CMake helpers for:
    1) stdlib module path detection
    2) target C++ module file-set wiring
]]
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/utils/DetectStdLibModulePaths.cmake)
detect_stdlib_module_paths()


# ==============================================================================
# Compiler Flags
# ==============================================================================
if(MSVC)
    add_compile_options(/std:c++latest)
    add_compile_options(/utf-8 /Zc:__cplusplus /Zc:preprocessor /bigobj)

   #[[ add_compile_options(/EHsc /W4 /utf-8 /Zc:__cplusplus /Zc:preprocessor /bigobj)

    # Use C7-compatible debug format (/Z7) instead of program database (/Zi)
    # Embeds debug symbols in .obj files instead of a single huge .pdb, bypassing 4GB limit
    add_compile_options(/Z7)
    
    # Disable excessive inlining (reduces code bloat)
    add_compile_options(/Ob1)
    # Disable whole program optimization (avoids merged object file bloat)
    add_compile_options(/GL-)
    # Define macro to suppress CRT security warnings (eliminates irrelevant warnings)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
]]
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    add_compile_options(
        -Wall -Wextra
        -Wno-reserved-module-identifier
        -Wno-include-angled-in-module-purview
    )
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-Wall -Wextra)
endif()

# ==============================================================================
# C++20 Module Scanning Configuration
# ==============================================================================
# Workaround: Disable module scanning for files that include stdexec headers
# stdexec uses digit separators (e.g., 2019'07L) which some module scanners reject
# Alternative: manually specify module dependencies or patch stdexec
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.28")
    # Option 1: Use a more lenient scanning mode (if available)
    # Option 2: Disable automatic scanning and manually specify dependencies
    # For now, we rely on the compiler's normal preprocessing which works fine

    # If the issue persists, consider:
    # set(CMAKE_CXX_SCAN_FOR_MODULES OFF)
    # and manually specify module dependencies
endif()

# ==============================================================================
# Third-party Library Configuration
# ==============================================================================
# stdexec: Header-only mode (not using add_subdirectory to avoid CPM dependency)
# Used for upper-layer sender/receiver organization, lower-layer I/O uses native coroutines
include_directories(SYSTEM ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/stdexec/include)

# jwt-cpp: Header-only JWT library (HS256/RS256, etc.)
include_directories(SYSTEM ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/jwt-cpp/include)

# nlohmann/json: C++ modules support (json.cppm)
include_directories(SYSTEM ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/json/include)
set(NLOHMANN_JSON_MODULE ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/json/src/modules/json.cppm)

# pugixml: Lightweight XML parser
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/pugixml/CMakeLists.txt")
    set(BUILD_SHARED_LIBS_SAVED ${BUILD_SHARED_LIBS})
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    add_subdirectory(3rdparty/pugixml EXCLUDE_FROM_ALL)
    set(BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS_SAVED} CACHE BOOL "" FORCE)
    set(CNETMOD_HAS_PUGIXML ON)
    message(STATUS "XML Parser: pugixml enabled")
else()
    set(CNETMOD_HAS_PUGIXML OFF)
    message(STATUS "XML Parser: pugixml not found in 3rdparty/")
endif()

# Logging module uses pure C++23 std::format implementation, no external dependencies

# nghttp2: HTTP/2 C library (build core library only)
option(CNETMOD_ENABLE_HTTP2 "Enable HTTP/2 support (requires nghttp2)" ON)
if(CNETMOD_ENABLE_HTTP2)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/nghttp2/CMakeLists.txt")
        # Configure nghttp2 build options before add_subdirectory
        set(ENABLE_LIB_ONLY ON CACHE BOOL "Build libnghttp2 only" FORCE)
        set(BUILD_SHARED_LIBS_SAVED ${BUILD_SHARED_LIBS})
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
        set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
        set(ENABLE_DOC OFF CACHE BOOL "" FORCE)
        set(BUILD_TESTING_SAVED ${BUILD_TESTING})
        set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
        set(ENABLE_FAILMALLOC OFF CACHE BOOL "" FORCE)

        add_subdirectory(3rdparty/nghttp2 EXCLUDE_FROM_ALL)

        # Restore parent project settings
        set(BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS_SAVED} CACHE BOOL "" FORCE)
        set(BUILD_TESTING ${BUILD_TESTING_SAVED} CACHE BOOL "" FORCE)

        set(CNETMOD_HAS_NGHTTP2 ON)
        message(STATUS "HTTP/2: Enabled (nghttp2)")
    else()
        set(CNETMOD_HAS_NGHTTP2 OFF)
        message(STATUS "HTTP/2: nghttp2 not found in 3rdparty/, HTTP/2 disabled")
    endif()
else()
    set(CNETMOD_HAS_NGHTTP2 OFF)
    message(STATUS "HTTP/2: Disabled by user")
endif()

# ==============================================================================
# SSL/TLS Support (Optional)
# ==============================================================================
option(CNETMOD_ENABLE_SSL "Enable SSL/TLS support (requires OpenSSL)" ON)
if(CNETMOD_ENABLE_SSL)
    find_package(OpenSSL QUIET)
    if(OpenSSL_FOUND)
        set(CNETMOD_HAS_SSL ON)
        message(STATUS "SSL/TLS: Enabled (OpenSSL ${OPENSSL_VERSION})")
    else()
        set(CNETMOD_HAS_SSL OFF)
        message(STATUS "SSL/TLS: OpenSSL not found, SSL support disabled")
    endif()
else()
    set(CNETMOD_HAS_SSL OFF)
    message(STATUS "SSL/TLS: Disabled by user")
endif()

# ==============================================================================
# zlib Compression Support (Optional)
# ==============================================================================
find_package(ZLIB QUIET)
if(ZLIB_FOUND)
    set(CNETMOD_HAS_ZLIB ON)
    message(STATUS "zlib: Found (${ZLIB_VERSION_STRING})")
else()
    set(CNETMOD_HAS_ZLIB OFF)
    message(STATUS "zlib: Not found, gzip compression middleware disabled")
endif()

# ==============================================================================
# Platform-specific I/O Backend Detection
# ==============================================================================
if(WIN32)
    set(CNETMOD_HAS_IOCP ON)
    message(STATUS "I/O backend: IOCP (Windows)")
elseif(APPLE)
    set(CNETMOD_HAS_KQUEUE ON)
    message(STATUS "I/O backend: kqueue (macOS)")
elseif(UNIX)
    # Detect io_uring support
    include(CheckIncludeFileCXX)
    check_include_file_cxx("liburing.h" CNETMOD_HAS_IO_URING)
    if(CNETMOD_HAS_IO_URING)
        message(STATUS "I/O backend: io_uring (Linux)")
    endif()
    # epoll is always available on Linux
    set(CNETMOD_HAS_EPOLL ON)
    message(STATUS "I/O backend: epoll (Linux)")
endif()

# ==============================================================================
# Source File Collection
# ==============================================================================

# Collect module interface files
file(GLOB_RECURSE MODULE_INTERFACE_FILES
    "src/*.cppm"
    "src/*.ixx"
)

# Collect implementation files (excluding main.cpp)
file(GLOB_RECURSE CPP_SOURCES "src/*.cpp")
list(REMOVE_ITEM CPP_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")

# Split .cpp sources into regular translation units and module implementation units.
# Module implementation units (.cpp with 'module X;') are regular sources, NOT CXX_MODULES.
# Only module interface/partition files (.cppm with 'export module') belong in CXX_MODULES.
set(MODULE_IMPLEMENTATION_CPP_FILES)
set(REGULAR_CPP_SOURCES)
foreach(src ${CPP_SOURCES})
    file(STRINGS "${src}" module_decl_line REGEX "^[ \t]*(export[ \t]+)?module[ \t]+")
    if(module_decl_line)
        list(APPEND MODULE_IMPLEMENTATION_CPP_FILES "${src}")
    else()
        list(APPEND REGULAR_CPP_SOURCES "${src}")
    endif()
endforeach()

# Filter by platform: Exclude I/O backend modules not needed for current platform
if(NOT CNETMOD_HAS_IOCP)
    list(FILTER MODULE_INTERFACE_FILES EXCLUDE REGEX "platform/iocp")
    list(FILTER MODULE_IMPLEMENTATION_CPP_FILES EXCLUDE REGEX "platform/iocp")
endif()
if(NOT CNETMOD_HAS_IO_URING)
    list(FILTER MODULE_INTERFACE_FILES EXCLUDE REGEX "platform/io_uring")
    list(FILTER MODULE_IMPLEMENTATION_CPP_FILES EXCLUDE REGEX "platform/io_uring")
endif()
if(NOT CNETMOD_HAS_EPOLL)
    list(FILTER MODULE_INTERFACE_FILES EXCLUDE REGEX "platform/epoll")
    list(FILTER MODULE_IMPLEMENTATION_CPP_FILES EXCLUDE REGEX "platform/epoll")
endif()
if(NOT CNETMOD_HAS_KQUEUE)
    list(FILTER MODULE_INTERFACE_FILES EXCLUDE REGEX "platform/kqueue")
    list(FILTER MODULE_IMPLEMENTATION_CPP_FILES EXCLUDE REGEX "platform/kqueue")
endif()
if(NOT CNETMOD_HAS_SSL)
    list(FILTER MODULE_INTERFACE_FILES EXCLUDE REGEX "core/ssl")
    list(FILTER MODULE_IMPLEMENTATION_CPP_FILES EXCLUDE REGEX "core/ssl")
endif()
if(NOT CNETMOD_HAS_NGHTTP2)
    list(FILTER MODULE_INTERFACE_FILES EXCLUDE REGEX "http/h2_")
    list(FILTER MODULE_INTERFACE_FILES EXCLUDE REGEX "http/stream_io")
    list(FILTER MODULE_IMPLEMENTATION_CPP_FILES EXCLUDE REGEX "http/h2_")
    list(FILTER MODULE_IMPLEMENTATION_CPP_FILES EXCLUDE REGEX "http/stream_io")
endif()

# ==============================================================================
# Core Library Target: cnetmod_core
# ==============================================================================

add_library(cnetmod_core STATIC
    ${REGULAR_CPP_SOURCES}
    ${MODULE_IMPLEMENTATION_CPP_FILES}
)

target_include_directories(cnetmod_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# Platform-specific macro definitions
target_compile_definitions(cnetmod_core PUBLIC
    $<$<PLATFORM_ID:Windows>:CNETMOD_PLATFORM_WINDOWS>
    $<$<PLATFORM_ID:Linux>:CNETMOD_PLATFORM_LINUX>
    $<$<PLATFORM_ID:Darwin>:CNETMOD_PLATFORM_MACOS>
    $<$<BOOL:${CNETMOD_HAS_IOCP}>:CNETMOD_HAS_IOCP>
    $<$<BOOL:${CNETMOD_HAS_IO_URING}>:CNETMOD_HAS_IO_URING>
    $<$<BOOL:${CNETMOD_HAS_EPOLL}>:CNETMOD_HAS_EPOLL>
    $<$<BOOL:${CNETMOD_HAS_KQUEUE}>:CNETMOD_HAS_KQUEUE>
    $<$<BOOL:${CNETMOD_HAS_SSL}>:CNETMOD_HAS_SSL>
    $<$<BOOL:${CNETMOD_HAS_ZLIB}>:CNETMOD_HAS_ZLIB>
    $<$<BOOL:${CNETMOD_HAS_NGHTTP2}>:CNETMOD_HAS_NGHTTP2>
)

# Platform-specific linking
if(WIN32)
    target_link_libraries(cnetmod_core PUBLIC ws2_32 mswsock)
elseif(CNETMOD_HAS_IO_URING)
    target_link_libraries(cnetmod_core PUBLIC uring)
endif()

# SSL/TLS linking
if(CNETMOD_HAS_SSL)
    target_link_libraries(cnetmod_core PUBLIC OpenSSL::SSL OpenSSL::Crypto)
endif()

# zlib linking
if(CNETMOD_HAS_ZLIB)
    target_link_libraries(cnetmod_core PUBLIC ZLIB::ZLIB)
endif()

# nghttp2 linking
if(CNETMOD_HAS_NGHTTP2)
    target_link_libraries(cnetmod_core PUBLIC nghttp2_static)
endif()

# pugixml linking
if(CNETMOD_HAS_PUGIXML)
    target_link_libraries(cnetmod_core PUBLIC pugixml-static)
endif()

# ==============================================================================
# C++ Module Configuration
# ==============================================================================
#[[
  Configure standard-library/project/json C++ module file sets on cnetmod_core.
]]
configure_cxx_modules(
    TARGET cnetmod_core
    MODULE_INTERFACE_FILES ${MODULE_INTERFACE_FILES}
    JSON_MODULE ${NLOHMANN_JSON_MODULE}
)

# Module generation mode
set_property(TARGET cnetmod_core PROPERTY
    CXX_MODULE_GENERATION_MODE "SEPARATE"
)

set(CNETMOD_LINK_TARGET cnetmod_core)

# ==============================================================================
# Main Executable
# ==============================================================================

add_executable(cnetmod_app src/main.cpp)
target_link_libraries(cnetmod_app PRIVATE ${CNETMOD_LINK_TARGET})
set_property(TARGET cnetmod_app PROPERTY
    CXX_MODULE_GENERATION_MODE "SEPARATE"
)

# ==============================================================================
# Testing, Benchmarks & TFB
# ==============================================================================

option(CNETMOD_BUILD_TESTS "Build tests" ON)
option(CNETMOD_BUILD_BENCH "Build performance benchmarks" ON)
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/testing/CMakeLists.txt")
    add_subdirectory(testing)
endif()

# ==============================================================================
# Example Program Configuration
# ==============================================================================

option(CNETMOD_BUILD_EXAMPLES "Build examples" ON)
if(CNETMOD_BUILD_EXAMPLES AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/CMakeLists.txt")
    add_subdirectory(examples)
endif()

# ==============================================================================
# Documentation Examples
# ==============================================================================

if(CNETMOD_BUILD_EXAMPLES AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/doc/CMakeLists.txt")
    add_subdirectory(doc)
endif()

# ==============================================================================
# Build Information Summary
# ==============================================================================

message(STATUS "==========================================")
message(STATUS "Project: ${PROJECT_NAME} v${PROJECT_VERSION}")
message(STATUS "C++ Standard: C++${CMAKE_CXX_STANDARD}")
message(STATUS "Compiler: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Platform: ${CMAKE_SYSTEM_NAME}")
if(STDLIB_MODULE_DIRS)
    message(STATUS "Standard Library Modules: ${STDLIB_MODULE_DIRS}")
endif()

# Linker information
if(CNETMOD_USING_MOLD)
    message(STATUS "Linker: mold (high-performance)")
else()
    message(STATUS "Linker: system default")
endif()

# Memory allocator information
if(CNETMOD_USING_MIMALLOC)
    message(STATUS "Memory Allocator: mimalloc (high-performance)")
else()
    message(STATUS "Memory Allocator: system default")
endif()

message(STATUS "==========================================")

# Print detailed optimization info if enabled
if(CNETMOD_USING_MOLD OR CNETMOD_USING_MIMALLOC)
    message(STATUS "")
    message(STATUS "Performance Optimizations Enabled:")
    if(CNETMOD_USING_MOLD)
        message(STATUS "  ✓ mold linker: 10-20x faster linking")
    endif()
    if(CNETMOD_USING_MIMALLOC)
        message(STATUS "  ✓ mimalloc: 2-3x faster memory allocation")
    endif()
    message(STATUS "")
endif()