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
# Auto-detect platform-specific standard library module paths
# ==============================================================================

if(UNIX)
    # Linux/Unix/macOS: Auto-detect libc++ standard library module paths
    
    # macOS special handling: Detect Homebrew LLVM path
    if(APPLE)
        # Apple Silicon (arm64) Homebrew path
        set(HOMEBREW_LLVM_PREFIX_ARM64 "/opt/homebrew/opt/llvm")
        # Intel Mac (x86_64) Homebrew path
        set(HOMEBREW_LLVM_PREFIX_X64 "/usr/local/opt/llvm")
        
        # Select Homebrew path based on architecture
        if(CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "arm64")
            set(HOMEBREW_LLVM_PREFIX ${HOMEBREW_LLVM_PREFIX_ARM64})
        else()
            set(HOMEBREW_LLVM_PREFIX ${HOMEBREW_LLVM_PREFIX_X64})
        endif()
        
        message(STATUS "macOS detection: arch=${CMAKE_HOST_SYSTEM_PROCESSOR}, Homebrew LLVM path=${HOMEBREW_LLVM_PREFIX}")
    endif()
    
    # Try to find libc++ module directory (LLVM 19-22)
    find_path(LIBCXX_MODULE_DIRS
        NAMES std.cppm
        PATHS
            # Linux paths
            /usr/lib/llvm-22/share/libc++/v1
            /usr/lib/llvm-21/share/libc++/v1
            /usr/lib/llvm-20/share/libc++/v1
            /usr/lib/llvm-19/share/libc++/v1
            /usr/local/lib/llvm-22/share/libc++/v1
            /usr/local/lib/llvm-21/share/libc++/v1
            /usr/local/lib/llvm-20/share/libc++/v1
            /usr/local/lib/llvm-19/share/libc++/v1
            /opt/llvm/share/libc++/v1
            # macOS Homebrew paths (Apple Silicon & Intel)
            /opt/homebrew/opt/llvm/share/libc++/v1
            /usr/local/opt/llvm/share/libc++/v1
        DOC "Path to libc++ standard library modules"
    )
    
    # Try to find libc++ header directory (LLVM 19-22)
    find_path(LIBCXX_INCLUDE_DIRS
        NAMES __config
        PATHS
            # Linux paths
            /usr/lib/llvm-22/include/c++/v1
            /usr/lib/llvm-21/include/c++/v1
            /usr/lib/llvm-20/include/c++/v1
            /usr/lib/llvm-19/include/c++/v1
            /usr/local/lib/llvm-22/include/c++/v1
            /usr/local/lib/llvm-21/include/c++/v1
            /usr/local/lib/llvm-20/include/c++/v1
            /usr/local/lib/llvm-19/include/c++/v1
            /opt/llvm/include/c++/v1
            /usr/include/c++/v1
            # macOS Homebrew paths (Apple Silicon & Intel)
            /opt/homebrew/opt/llvm/include/c++/v1
            /usr/local/opt/llvm/include/c++/v1
        DOC "Path to libc++ headers"
    )
    
    if(LIBCXX_MODULE_DIRS AND LIBCXX_INCLUDE_DIRS)
        message(STATUS "Auto-detected libc++ paths:")
        message(STATUS "  Module directory: ${LIBCXX_MODULE_DIRS}")
        message(STATUS "  Header directory: ${LIBCXX_INCLUDE_DIRS}")
        
        # Force all targets to use libc++ (must be set before adding third-party libraries)
        if(APPLE)
            # macOS: Need to specify sysroot and LLVM libc++ library path
            # Get macOS SDK path
            execute_process(
                COMMAND xcrun --show-sdk-path
                OUTPUT_VARIABLE MACOS_SDK_PATH
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
            message(STATUS "macOS SDK path: ${MACOS_SDK_PATH}")
            
            # Infer LLVM installation path from module directory
            get_filename_component(LLVM_LIB_PATH "${LIBCXX_MODULE_DIRS}/../../../lib/c++" ABSOLUTE)
            message(STATUS "LLVM libc++ library path: ${LLVM_LIB_PATH}")
            
            add_compile_options(
                -nostdinc++
                -isystem ${LIBCXX_INCLUDE_DIRS}
                -isysroot ${MACOS_SDK_PATH}
            )
            # macOS link options: Specify LLVM libc++ path to avoid conflicts with system libc++
            add_link_options(
                -stdlib=libc++
                -L${LLVM_LIB_PATH}
                -Wl,-rpath,${LLVM_LIB_PATH}
                -lc++
                -lc++abi
            )
        else()
            # Linux
            add_compile_options(
                -nostdinc++
                -isystem ${LIBCXX_INCLUDE_DIRS}
            )
            add_link_options(-stdlib=libc++ -lc++ -lc++abi)
        endif()
    else()
        message(WARNING "Unable to auto-detect libc++ paths. Please set manually:")
        message(WARNING "  cmake -DLIBCXX_MODULE_DIRS=<path> -DLIBCXX_INCLUDE_DIRS=<path> ..")
    endif()
    
elseif(WIN32)
    # Windows: Auto-detect MSVC standard library module paths
    
    # Priority 1: Try to get from VCToolsInstallDir environment variable
    if(DEFINED ENV{VCToolsInstallDir})
        set(LIBCXX_MODULE_DIRS "$ENV{VCToolsInstallDir}/modules" CACHE PATH "Path to MSVC modules")
        if(EXISTS "${LIBCXX_MODULE_DIRS}/std.ixx")
            message(STATUS "Detected MSVC module directory from VCToolsInstallDir environment variable: ${LIBCXX_MODULE_DIRS}")
        else()
            message(WARNING "Found VCToolsInstallDir but std.ixx does not exist at: ${LIBCXX_MODULE_DIRS}")
            unset(LIBCXX_MODULE_DIRS CACHE)
        endif()
    endif()
    
    # Priority 2: If environment variable not found or invalid, try to detect from compiler path
    if(NOT LIBCXX_MODULE_DIRS)
        # Extract MSVC version directory from compiler path
        # cl.exe is located at: VC/Tools/MSVC/{version}/bin/Hostx64/x64/cl.exe
        # modules are located at: VC/Tools/MSVC/{version}/modules/
        # So we need to go up 3 levels from cl.exe directory
        get_filename_component(COMPILER_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
        get_filename_component(COMPILER_HOST_DIR "${COMPILER_DIR}" DIRECTORY)
        get_filename_component(COMPILER_BIN_DIR "${COMPILER_HOST_DIR}" DIRECTORY)
        get_filename_component(MSVC_VERSION_DIR "${COMPILER_BIN_DIR}" DIRECTORY)
        
        # First try the path extracted from compiler path
        set(DETECTED_MODULE_PATH "${MSVC_VERSION_DIR}/modules")
        
        if(EXISTS "${DETECTED_MODULE_PATH}/std.ixx")
            set(LIBCXX_MODULE_DIRS "${DETECTED_MODULE_PATH}" CACHE PATH "Path to MSVC modules")
            message(STATUS "Auto-detected MSVC module directory from compiler path: ${LIBCXX_MODULE_DIRS}")
        else()
            # Fallback: Search common Visual Studio installation locations
            message(STATUS "Unable to detect from compiler path, searching common installation locations...")
            find_path(LIBCXX_MODULE_DIRS
                NAMES std.ixx
                PATHS
                    "C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC"
                    "C:/Program Files/Microsoft Visual Studio/2022/Professional/VC/Tools/MSVC"
                    "C:/Program Files/Microsoft Visual Studio/2022/Enterprise/VC/Tools/MSVC"
                    "F:/program/visul_studio/idea/VC/Tools/MSVC"
                PATH_SUFFIXES
                    "14.44.35207/modules"
                    "14.43.34601/modules"
                    "14.42.34433/modules"
                DOC "Path to MSVC standard library modules"
            )
            
            if(LIBCXX_MODULE_DIRS)
                message(STATUS "Found MSVC module directory at common location: ${LIBCXX_MODULE_DIRS}")
            else()
                message(WARNING "Unable to auto-detect MSVC module path. Please set manually:")
                message(WARNING "  cmake -DLIBCXX_MODULE_DIRS=<path> ..")
            endif()
        endif()
    endif()
endif()


# ==============================================================================
# Compiler Flags
# ==============================================================================
if(MSVC)
    add_compile_options(/EHsc /W4 /utf-8 /Zc:__cplusplus /Zc:preprocessor /bigobj)

    # Use C7-compatible debug format (/Z7) instead of program database (/Zi)
    # Embeds debug symbols in .obj files instead of a single huge .pdb, bypassing 4GB limit
    add_compile_options(/Z7)
    
    # Disable excessive inlining (reduces code bloat)
    add_compile_options(/Ob1)
    # Disable whole program optimization (avoids merged object file bloat)
    add_compile_options(/GL-)
    # Define macro to suppress CRT security warnings (eliminates irrelevant warnings)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)

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

# Filter by platform: Exclude I/O backend modules not needed for current platform
if(NOT CNETMOD_HAS_IOCP)
    list(FILTER MODULE_INTERFACE_FILES EXCLUDE REGEX "platform/iocp")
endif()
if(NOT CNETMOD_HAS_IO_URING)
    list(FILTER MODULE_INTERFACE_FILES EXCLUDE REGEX "platform/io_uring")
endif()
if(NOT CNETMOD_HAS_EPOLL)
    list(FILTER MODULE_INTERFACE_FILES EXCLUDE REGEX "platform/epoll")
endif()
if(NOT CNETMOD_HAS_KQUEUE)
    list(FILTER MODULE_INTERFACE_FILES EXCLUDE REGEX "platform/kqueue")
endif()
if(NOT CNETMOD_HAS_SSL)
    list(FILTER MODULE_INTERFACE_FILES EXCLUDE REGEX "core/ssl")
endif()
if(NOT CNETMOD_HAS_NGHTTP2)
    list(FILTER MODULE_INTERFACE_FILES EXCLUDE REGEX "http/h2_")
    list(FILTER MODULE_INTERFACE_FILES EXCLUDE REGEX "http/stream_io")
endif()

# ==============================================================================
# Core Library Target: cnetmod_core
# ==============================================================================

add_library(cnetmod_core STATIC
    ${CPP_SOURCES}
    ${MODULE_INTERFACE_FILES}
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

# ==============================================================================
# C++ Module Configuration
# ==============================================================================

if(UNIX AND LIBCXX_MODULE_DIRS AND LIBCXX_INCLUDE_DIRS)
    # Linux/macOS: libc++ standard library modules
    target_include_directories(cnetmod_core SYSTEM PUBLIC
        ${LIBCXX_INCLUDE_DIRS}
    )
    target_sources(cnetmod_core PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR} ${LIBCXX_MODULE_DIRS}
        FILES
            ${MODULE_INTERFACE_FILES}
            ${LIBCXX_MODULE_DIRS}/std.cppm
            ${LIBCXX_MODULE_DIRS}/std.compat.cppm
            ${NLOHMANN_JSON_MODULE}
    )
elseif(WIN32 AND LIBCXX_MODULE_DIRS)
    # Windows: MSVC standard library modules
    target_sources(cnetmod_core PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR} ${LIBCXX_MODULE_DIRS}
        FILES
            ${MODULE_INTERFACE_FILES}
            ${LIBCXX_MODULE_DIRS}/std.ixx
            ${LIBCXX_MODULE_DIRS}/std.compat.ixx
            ${NLOHMANN_JSON_MODULE}
    )
else()
    message(WARNING "Standard library module path not detected, please set LIBCXX_MODULE_DIRS manually")
    target_sources(cnetmod_core PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        FILES ${MODULE_INTERFACE_FILES} ${NLOHMANN_JSON_MODULE}
    )
endif()

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
if(LIBCXX_MODULE_DIRS)
    message(STATUS "Standard Library Modules: ${LIBCXX_MODULE_DIRS}")
endif()
message(STATUS "==========================================")