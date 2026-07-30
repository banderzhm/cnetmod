
cmake_minimum_required(VERSION 3.28)

project(cnetmod
    VERSION 2.0.0
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

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Protocols.cmake)

# ==============================================================================
# Linker Optimization: Use mold if available (Linux only)
# ==============================================================================
option(CNETMOD_USE_MOLD "Use mold linker for faster linking (Linux only)" ON)
if(CNETMOD_USE_MOLD)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/3rdparty/Mold.cmake)
    cnetmod_use_mold()
endif()
# ==============================================================================
# Memory Allocator Optimization: Use mimalloc if available
# ==============================================================================
option(CNETMOD_USE_MIMALLOC "Use mimalloc allocator for better performance" ON)
if(CNETMOD_USE_MIMALLOC)
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/3rdparty/Mimalloc.cmake)
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
    add_compile_options(/utf-8 /Zc:__cplusplus /Zc:preprocessor /bigobj /FS /wd4737)
    # Keep debug information in each object file. The core target contains many
    # module translation units; a shared compiler PDB can otherwise become a
    # point of contention even when /FS is enabled.
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
option(CNETMOD_USE_SYSTEM_DEPS
    "Prefer dependencies provided by the active toolchain/package manager before bundled 3rdparty copies"
    ON)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/3rdparty/ThirdPartyDependencies.cmake)
cnetmod_configure_third_party_dependencies()

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
        include(CheckCXXSourceCompiles)
        check_cxx_source_compiles(
            "#include <liburing.h>\nint main() { io_uring_buf_reg reg{}; io_uring_buf_ring* ring = nullptr; return sizeof(reg) + sizeof(*ring); }"
            CNETMOD_HAS_IO_URING_BUFFER_RING)
    endif()
    # epoll is always available on Linux
    set(CNETMOD_HAS_EPOLL ON)
    message(STATUS "I/O backend: epoll (Linux)")
endif()

# ==============================================================================
# Source File Collection
# ==============================================================================

# Collect module interface files
file(GLOB_RECURSE MODULE_INTERFACE_FILES CONFIGURE_DEPENDS
    "src/*.cppm"
    "src/*.ixx"
)

# Collect implementation files (excluding main.cpp)
file(GLOB_RECURSE CPP_SOURCES CONFIGURE_DEPENDS "src/*.cpp")
list(REMOVE_ITEM CPP_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")

# Split .cpp sources into regular translation units, module implementation units,
# and interface units. Most interfaces use .cppm, but a renamed interface may
# retain an `export module` declaration in a .cpp file.
set(MODULE_IMPLEMENTATION_CPP_FILES)
set(REGULAR_CPP_SOURCES)
foreach(src ${CPP_SOURCES})
    file(STRINGS "${src}" export_module_decl_line
        REGEX "^[ \t]*export[ \t]+module[ \t]+")
    file(STRINGS "${src}" module_decl_line
        REGEX "^[ \t]*(export[ \t]+)?module[ \t]+")
    if(export_module_decl_line)
        list(APPEND MODULE_INTERFACE_FILES "${src}")
    elseif(module_decl_line)
        list(APPEND MODULE_IMPLEMENTATION_CPP_FILES "${src}")
    else()
        list(APPEND REGULAR_CPP_SOURCES "${src}")
    endif()
endforeach()

cnetmod_filter_disabled_protocol_sources(
    VARIABLES
        MODULE_INTERFACE_FILES
        MODULE_IMPLEMENTATION_CPP_FILES
        REGULAR_CPP_SOURCES
)
cnetmod_filter_disabled_component_sources(
    VARIABLES
        MODULE_INTERFACE_FILES
        MODULE_IMPLEMENTATION_CPP_FILES
        REGULAR_CPP_SOURCES
)

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

    # MSVC 19.44 can fail required coroutine tail-call codegen (C4737) in this
    # large Release translation unit. Keep the workaround scoped to the file.
    set_source_files_properties(
        "${CMAKE_CURRENT_SOURCE_DIR}/src/protocol/http/client/http_client.cpp"
        PROPERTIES COMPILE_OPTIONS "/Od;/Ob0"
    )
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
# Keep the SSL and DTLS module units in non-OpenSSL builds.  Their declarations
# and implementations are already guarded by CNETMOD_HAS_SSL, so they become
# empty compatibility modules without introducing an OpenSSL link dependency.
# Other protocol modules can then import their stable module names while
# compiling their own TLS code out.  Removing the units here made MSVC fail
# dependency scanning with C2230 before those feature guards were considered.

# ============================================================================== 
# Core Library Target: cnetmod_core
# ============================================================================== 

add_library(cnetmod_core STATIC
    ${REGULAR_CPP_SOURCES}
    ${MODULE_IMPLEMENTATION_CPP_FILES}
)

target_include_directories(cnetmod_core PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# Platform-specific macro definitions
target_compile_definitions(cnetmod_core PUBLIC
    $<$<PLATFORM_ID:Windows>:CNETMOD_PLATFORM_WINDOWS>
    $<$<PLATFORM_ID:Linux>:CNETMOD_PLATFORM_LINUX>
    $<$<PLATFORM_ID:Darwin>:CNETMOD_PLATFORM_MACOS>
    $<$<BOOL:${CNETMOD_HAS_IOCP}>:CNETMOD_HAS_IOCP>
    $<$<BOOL:${CNETMOD_HAS_IO_URING}>:CNETMOD_HAS_IO_URING>
    $<$<BOOL:${CNETMOD_HAS_IO_URING_BUFFER_RING}>:CNETMOD_HAS_IO_URING_BUFFER_RING>
    $<$<BOOL:${CNETMOD_HAS_EPOLL}>:CNETMOD_HAS_EPOLL>
    $<$<BOOL:${CNETMOD_HAS_KQUEUE}>:CNETMOD_HAS_KQUEUE>
    $<$<BOOL:${CNETMOD_HAS_SSL}>:CNETMOD_HAS_SSL>
    $<$<BOOL:${CNETMOD_HAS_ZLIB}>:CNETMOD_HAS_ZLIB>
    $<$<BOOL:${CNETMOD_HAS_LZ4}>:CNETMOD_HAS_LZ4>
    $<$<BOOL:${CNETMOD_HAS_LEVELDB}>:CNETMOD_HAS_LEVELDB>
    $<$<BOOL:${CNETMOD_HAS_ICU}>:CNETMOD_HAS_ICU>
    $<$<BOOL:${CNETMOD_ENABLE_ORM}>:CNETMOD_HAS_ORM>
)
foreach(protocol IN LISTS CNETMOD_PROTOCOLS)
    if(CNETMOD_ENABLE_${protocol})
        target_compile_definitions(cnetmod_core PUBLIC
            CNETMOD_HAS_PROTOCOL_${protocol})
    endif()
endforeach()
add_library(cnetmod::core ALIAS cnetmod_core)

# Platform-specific linking
if(WIN32)
    target_link_libraries(cnetmod_core PUBLIC ws2_32 mswsock)
elseif(CNETMOD_HAS_IO_URING)
    target_link_libraries(cnetmod_core PUBLIC uring)
endif()

cnetmod_link_third_party_dependencies(cnetmod_core)

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

# ============================================================================== 
# Installable binary package / GitHub Release asset
# ============================================================================== 
# The package contains the static core library, public headers, C++ module
# interfaces, and the exported CMake target.  Consumers must use a compatible
# compiler/toolchain because C++ module BMIs are toolchain-specific.
if(CNETMOD_HAS_LEVELDB AND CNETMOD_LEVELDB_TARGET STREQUAL "leveldb")
    install(TARGETS leveldb
        EXPORT cnetmodTargets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    )
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/leveldb/include/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
endif()
install(TARGETS cnetmod_core
    EXPORT cnetmodTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    FILE_SET cxx_modules DESTINATION "${CMAKE_INSTALL_LIBDIR}/cnetmod/modules"
)
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cnetmodConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/cnetmodConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/cnetmod"
)
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/cnetmodConfigVersion.cmake"
    VERSION "${PROJECT_VERSION}"
    COMPATIBILITY SameMajorVersion
)
install(EXPORT cnetmodTargets
    FILE cnetmodTargets.cmake
    NAMESPACE cnetmod::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/cnetmod"
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/cnetmodConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/cnetmodConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/cnetmod"
)

set(CPACK_PACKAGE_NAME "cnetmod")
set(CPACK_PACKAGE_VENDOR "banderzhm")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "cnetmod-${PROJECT_VERSION}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
set(CPACK_GENERATOR "ZIP")
include(CPack)

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
        message(STATUS "  鉁?mold linker: 10-20x faster linking")
    endif()
    if(CNETMOD_USING_MIMALLOC)
        message(STATUS "  鉁?mimalloc: 2-3x faster memory allocation")
    endif()
    message(STATUS "")
endif()
