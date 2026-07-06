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
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/utils/UseMold.cmake)
    cnetmod_use_mold()
endif()

# ==============================================================================
# Memory Allocator Optimization: Use mimalloc if available
# ==============================================================================
option(CNETMOD_USE_MIMALLOC "Use mimalloc allocator for better performance" ON)
if(CNETMOD_USE_MIMALLOC)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/utils/UseMimalloc.cmake)
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
    add_compile_options(/utf-8 /Zc:__cplusplus /Zc:preprocessor /bigobj /FS)
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT Embedded)

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
        -Wno-unused-command-line-argument
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
# Header-only third-party paths are cache variables so a parent project can
# reuse its own dependency checkout when cnetmod is added as a subdirectory.
set(CNETMOD_STDEXEC_INCLUDE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/stdexec/include"
    CACHE PATH "stdexec include directory used by cnetmod")
set(CNETMOD_JWT_CPP_INCLUDE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/jwt-cpp/include"
    CACHE PATH "jwt-cpp include directory used by cnetmod")
set(CNETMOD_JSON_INCLUDE_DIR
    "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/json/include"
    CACHE PATH "nlohmann/json include directory used by cnetmod")
set(CNETMOD_JSON_MODULE
    "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/json/src/modules/json.cppm"
    CACHE FILEPATH "nlohmann/json C++ module interface used by cnetmod")

# stdexec: Header-only mode (not using add_subdirectory to avoid CPM dependency)
# Used for upper-layer sender/receiver organization, lower-layer I/O uses native coroutines
if(NOT EXISTS "${CNETMOD_STDEXEC_INCLUDE_DIR}/exec/static_thread_pool.hpp")
    message(FATAL_ERROR
        "stdexec headers were not found at ${CNETMOD_STDEXEC_INCLUDE_DIR}. "
        "Initialize submodules with: git submodule update --init --recursive")
endif()
set(CNETMOD_STDEXEC_EFFECTIVE_INCLUDE_DIR "${CNETMOD_STDEXEC_INCLUDE_DIR}")
set(CNETMOD_STDEXEC_EFFECTIVE_INCLUDE_DIR
    "${CNETMOD_STDEXEC_EFFECTIVE_INCLUDE_DIR}"
    CACHE INTERNAL "Effective stdexec include directory after toolchain-specific preparation" FORCE)
include_directories(SYSTEM ${CNETMOD_STDEXEC_EFFECTIVE_INCLUDE_DIR})

# jwt-cpp: Header-only JWT library (HS256/RS256, etc.)
include_directories(SYSTEM ${CNETMOD_JWT_CPP_INCLUDE_DIR})

# nlohmann/json: C++ modules support (json.cppm)
include_directories(SYSTEM ${CNETMOD_JSON_INCLUDE_DIR})
set(NLOHMANN_JSON_MODULE ${CNETMOD_JSON_MODULE})

# pugixml: Lightweight XML parser
if(TARGET pugixml::pugixml)
    set(CNETMOD_HAS_PUGIXML ON)
    set(CNETMOD_PUGIXML_TARGET pugixml::pugixml)
    message(STATUS "XML Parser: pugixml enabled (existing target pugixml::pugixml)")
elseif(TARGET pugixml-static)
    set(CNETMOD_HAS_PUGIXML ON)
    set(CNETMOD_PUGIXML_TARGET pugixml-static)
    message(STATUS "XML Parser: pugixml enabled (existing target pugixml-static)")
elseif(TARGET pugixml)
    set(CNETMOD_HAS_PUGIXML ON)
    set(CNETMOD_PUGIXML_TARGET pugixml)
    message(STATUS "XML Parser: pugixml enabled (existing target pugixml)")
elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/pugixml/CMakeLists.txt")
    set(BUILD_SHARED_LIBS_SAVED ${BUILD_SHARED_LIBS})
    set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/3rdparty/pugixml")
    add_subdirectory(3rdparty/pugixml EXCLUDE_FROM_ALL)
    set(BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS_SAVED} CACHE BOOL "" FORCE)
    set(CNETMOD_HAS_PUGIXML ON)
    set(CNETMOD_PUGIXML_TARGET pugixml-static)
    message(STATUS "XML Parser: pugixml enabled")
else()
    set(CNETMOD_HAS_PUGIXML OFF)
    message(STATUS "XML Parser: pugixml not found in 3rdparty/")
endif()

# Logging module uses pure C++23 std::format implementation, no external dependencies

# nghttp2: HTTP/2 C library (build core library only)
option(CNETMOD_ENABLE_HTTP2 "Enable HTTP/2 support (requires nghttp2)" ON)
if(CNETMOD_ENABLE_HTTP2)
    if(TARGET nghttp2_static)
        set(CNETMOD_HAS_NGHTTP2 ON)
        set(CNETMOD_NGHTTP2_TARGET nghttp2_static)
        message(STATUS "HTTP/2: Enabled (existing target nghttp2_static)")
    elseif(TARGET nghttp2)
        set(CNETMOD_HAS_NGHTTP2 ON)
        set(CNETMOD_NGHTTP2_TARGET nghttp2)
        message(STATUS "HTTP/2: Enabled (existing target nghttp2)")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/nghttp2/CMakeLists.txt")
        # Configure nghttp2 build options before add_subdirectory
        set(ENABLE_LIB_ONLY ON CACHE BOOL "Build libnghttp2 only" FORCE)
        set(BUILD_SHARED_LIBS_SAVED ${BUILD_SHARED_LIBS})
        set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
        set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
        set(ENABLE_DOC OFF CACHE BOOL "" FORCE)
        set(BUILD_TESTING_SAVED ${BUILD_TESTING})
        set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
        set(ENABLE_FAILMALLOC OFF CACHE BOOL "" FORCE)

        # nghttp2 configures a few generated files before its subdirectories
        # are added; pre-create those binary directories for stable reconfigure.
        set(CNETMOD_NGHTTP2_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/3rdparty/nghttp2")
        file(MAKE_DIRECTORY
            "${CNETMOD_NGHTTP2_BINARY_DIR}/lib"
            "${CNETMOD_NGHTTP2_BINARY_DIR}/lib/includes/nghttp2"
            "${CNETMOD_NGHTTP2_BINARY_DIR}/integration-tests"
            "${CNETMOD_NGHTTP2_BINARY_DIR}/doc"
        )

        add_subdirectory(3rdparty/nghttp2 EXCLUDE_FROM_ALL)

        # Restore parent project settings
        set(BUILD_SHARED_LIBS ${BUILD_SHARED_LIBS_SAVED} CACHE BOOL "" FORCE)
        set(BUILD_TESTING ${BUILD_TESTING_SAVED} CACHE BOOL "" FORCE)

        set(CNETMOD_HAS_NGHTTP2 ON)
        set(CNETMOD_NGHTTP2_TARGET nghttp2_static)
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
# LevelDB Support (Optional, used by Raft persistent storage)
# ==============================================================================
option(CNETMOD_ENABLE_LEVELDB "Enable LevelDB-backed Raft storage when LevelDB is available" ON)
if(CNETMOD_ENABLE_LEVELDB)
    if(TARGET leveldb)
        set(CNETMOD_HAS_LEVELDB ON)
        set(CNETMOD_LEVELDB_TARGET leveldb)
        message(STATUS "LevelDB: Enabled (existing target leveldb)")
    elseif(TARGET leveldb::leveldb)
        set(CNETMOD_HAS_LEVELDB ON)
        set(CNETMOD_LEVELDB_TARGET leveldb::leveldb)
        message(STATUS "LevelDB: Enabled (existing target leveldb::leveldb)")
    elseif(TARGET LevelDB::LevelDB)
        set(CNETMOD_HAS_LEVELDB ON)
        set(CNETMOD_LEVELDB_TARGET LevelDB::LevelDB)
        message(STATUS "LevelDB: Enabled (existing target LevelDB::LevelDB)")
    elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/leveldb/CMakeLists.txt")
        set(LEVELDB_BUILD_TESTS OFF CACHE BOOL "" FORCE)
        set(LEVELDB_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
        set(LEVELDB_INSTALL OFF CACHE BOOL "" FORCE)
        add_subdirectory(3rdparty/leveldb EXCLUDE_FROM_ALL)
        if(TARGET leveldb)
            # LevelDB adds PUBLIC -Werror when clang thread-safety analysis is
            # available. Keep its warnings visible, but do not let third-party
            # warnings fail cnetmod builds or propagate -Werror to consumers.
            foreach(prop COMPILE_OPTIONS INTERFACE_COMPILE_OPTIONS)
                get_target_property(leveldb_options leveldb ${prop})
                if(leveldb_options)
                    list(REMOVE_ITEM leveldb_options -Werror)
                    set_target_properties(leveldb PROPERTIES ${prop} "${leveldb_options}")
                endif()
            endforeach()
        endif()
        set(CNETMOD_HAS_LEVELDB ON)
        set(CNETMOD_LEVELDB_TARGET leveldb)
        message(STATUS "LevelDB: Enabled (3rdparty/leveldb)")
    else()
        find_package(LevelDB QUIET)
    endif()
    if(LevelDB_FOUND AND NOT CNETMOD_HAS_LEVELDB)
        set(CNETMOD_HAS_LEVELDB ON)
        if(TARGET leveldb::leveldb)
            set(CNETMOD_LEVELDB_TARGET leveldb::leveldb)
        elseif(TARGET LevelDB::LevelDB)
            set(CNETMOD_LEVELDB_TARGET LevelDB::LevelDB)
        elseif(LevelDB_LIBRARIES)
            set(CNETMOD_LEVELDB_LIBRARY ${LevelDB_LIBRARIES})
            set(CNETMOD_LEVELDB_INCLUDE_DIR ${LevelDB_INCLUDE_DIRS})
        endif()
        message(STATUS "LevelDB: Enabled (CMake package)")
    elseif(NOT CNETMOD_HAS_LEVELDB)
        find_path(CNETMOD_LEVELDB_INCLUDE_DIR leveldb/db.h)
        find_library(CNETMOD_LEVELDB_LIBRARY NAMES leveldb)
        if(CNETMOD_LEVELDB_INCLUDE_DIR AND CNETMOD_LEVELDB_LIBRARY)
            set(CNETMOD_HAS_LEVELDB ON)
            message(STATUS "LevelDB: Enabled (${CNETMOD_LEVELDB_LIBRARY})")
        else()
            set(CNETMOD_HAS_LEVELDB OFF)
            message(STATUS "LevelDB: Not found, Raft LevelDB storage disabled")
        endif()
    endif()
else()
    set(CNETMOD_HAS_LEVELDB OFF)
    message(STATUS "LevelDB: Disabled by user")
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

# MSVC's module scanner defaults ordinary .cpp scan output to "$(IntDir)/.d.json".
# Multiple module implementation units then share the same dependency output and
# make the Visual Studio target permanently out of date. Give every .cpp source a
# stable per-file dependency path so repeated builds can be true no-ops.
if(MSVC)
    foreach(src ${CPP_SOURCES})
        file(RELATIVE_PATH src_rel "${CMAKE_CURRENT_SOURCE_DIR}" "${src}")
        string(MAKE_C_IDENTIFIER "${src_rel}" src_dep_name)
        set_source_files_properties("${src}" PROPERTIES
            VS_SETTINGS "ModuleDependenciesFile=$(IntDir)/${src_dep_name}.module.json"
        )
    endforeach()
endif()

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
    $<$<BOOL:${CNETMOD_HAS_LEVELDB}>:CNETMOD_HAS_LEVELDB>
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
    target_link_libraries(cnetmod_core PUBLIC ${CNETMOD_NGHTTP2_TARGET})
endif()

# pugixml linking
if(CNETMOD_HAS_PUGIXML)
    target_link_libraries(cnetmod_core PUBLIC ${CNETMOD_PUGIXML_TARGET})
endif()

# LevelDB linking
if(CNETMOD_HAS_LEVELDB)
    if(CNETMOD_LEVELDB_TARGET)
        target_link_libraries(cnetmod_core PUBLIC ${CNETMOD_LEVELDB_TARGET})
    else()
        target_include_directories(cnetmod_core PUBLIC ${CNETMOD_LEVELDB_INCLUDE_DIR})
        target_link_libraries(cnetmod_core PUBLIC ${CNETMOD_LEVELDB_LIBRARY})
    endif()
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
if(CNETMOD_BUILD_TESTS)
    enable_testing()
endif()
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
# Aggregate Build Targets
# ==============================================================================

set(CNETMOD_ALL_TARGETS
    cnetmod_core
    cnetmod_app
    ${CNETMOD_TEST_TARGETS}
    ${CNETMOD_BENCH_TARGETS}
    ${CNETMOD_EXAMPLE_TARGETS}
)

list(REMOVE_DUPLICATES CNETMOD_ALL_TARGETS)

add_custom_target(cnetmod_build_all
    DEPENDS ${CNETMOD_ALL_TARGETS}
    COMMENT "Build cnetmod core, app, tests, benchmarks, and examples"
)

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
