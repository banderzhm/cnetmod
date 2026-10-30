
cmake_minimum_required(VERSION 3.28)

# CMake 4.2 preserves backslashes passed through macros. Component extraction
# regexes intentionally match both Windows and POSIX separators, so use the
# non-lossy argument semantics when the policy is available while retaining
# compatibility with the declared 3.28 minimum.
if(POLICY CMP0219)
    cmake_policy(SET CMP0219 NEW)
endif()

file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/VERSION" CNETMOD_VERSION
    LIMIT_COUNT 1)
if(NOT CNETMOD_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+$")
    message(FATAL_ERROR
        "VERSION must contain one semantic version: ${CNETMOD_VERSION}")
endif()

project(cnetmod
    VERSION "${CNETMOD_VERSION}"
    DESCRIPTION "Cross-platform asynchronous network library with C++23 modules"
    LANGUAGES CXX
)

set(CNETMOD_GENERATED_INCLUDE_DIR
    "${CMAKE_CURRENT_BINARY_DIR}/generated/include")
file(MAKE_DIRECTORY "${CNETMOD_GENERATED_INCLUDE_DIR}/cnetmod")
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/include/cnetmod/version.hpp.in"
    "${CNETMOD_GENERATED_INCLUDE_DIR}/cnetmod/version.hpp"
    @ONLY)

# Cloud-sync clients can transiently lock Visual Studio's generated
# ZERO_CHECK.lastbuildstate file.  CLion/CMake already reconfigures explicitly
# when project inputs change, so avoid a redundant regeneration target on
# Windows and keep ordinary builds independent of that external file lock.
if(WIN32 AND CMAKE_GENERATOR MATCHES "Visual Studio")
    set(CMAKE_SUPPRESS_REGENERATION ON)
endif()

# ==============================================================================
# C++ Standard Configuration
# ==============================================================================
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Some package/toolchain setups clear CMAKE_CXX_FLAGS_RELEASE.  Keep the
# language-level Release contract intact even in that case: the standard
# library and cnetmod both use NDEBUG to remove assertion-only work.  MSVC
# already supplies it for these configurations; a duplicate definition is
# harmless, while single-config Unix generators need this explicit fallback.
if(CMAKE_CONFIGURATION_TYPES)
    add_compile_definitions(
        "$<$<CONFIG:Release>:NDEBUG>"
        "$<$<CONFIG:RelWithDebInfo>:NDEBUG>"
        "$<$<CONFIG:MinSizeRel>:NDEBUG>")
elseif(CMAKE_BUILD_TYPE MATCHES "^(Release|RelWithDebInfo|MinSizeRel)$")
    add_compile_definitions(NDEBUG)
endif()

# The same toolchains also drop the optimization level, which silently turns
# an optimized configuration into an -O0 build.  Restore the CMake default
# level only for configurations whose flags carry none.
if(NOT MSVC)
    set(_cnetmod_optimization_RELEASE -O3)
    set(_cnetmod_optimization_RELWITHDEBINFO -O2)
    set(_cnetmod_optimization_MINSIZEREL -Os)
    foreach(_cnetmod_config RELEASE RELWITHDEBINFO MINSIZEREL)
        if(NOT "${CMAKE_CXX_FLAGS} ${CMAKE_CXX_FLAGS_${_cnetmod_config}}"
                MATCHES "(^| )-O")
            if(_cnetmod_config STREQUAL "RELWITHDEBINFO")
                set(_cnetmod_config_name RelWithDebInfo)
            elseif(_cnetmod_config STREQUAL "MINSIZEREL")
                set(_cnetmod_config_name MinSizeRel)
            else()
                set(_cnetmod_config_name Release)
            endif()
            add_compile_options(
                "$<$<CONFIG:${_cnetmod_config_name}>:${_cnetmod_optimization_${_cnetmod_config}}>")
        endif()
    endforeach()
endif()

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
# Memory Allocator Optimization: bundled mimalloc, enabled on final executables
# ==============================================================================
option(CNETMOD_USE_MIMALLOC "Use mimalloc allocator for better performance" ON)
set(CNETMOD_DEPENDENCY_MODE "system" CACHE STRING
    "Dependency source: system, vcpkg, or conan")
set_property(CACHE CNETMOD_DEPENDENCY_MODE PROPERTY STRINGS system vcpkg conan)
if(NOT CNETMOD_DEPENDENCY_MODE MATCHES "^(system|vcpkg|conan)$")
    message(FATAL_ERROR
        "CNETMOD_DEPENDENCY_MODE must be system, vcpkg, or conan")
endif()
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/3rdparty/Mimalloc.cmake)
set(CNETMOD_USING_MIMALLOC FALSE CACHE BOOL "Using bundled mimalloc" FORCE)
if(CNETMOD_USE_MIMALLOC)
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
    # A static library archives its object files verbatim.  Do not embed C7
    # debug information (/Z7) in Release or MinSizeRel objects: with the large
    # number of module translation units this makes the distributable archive
    # hundreds of megabytes larger.  Retain the existing embedded debug info
    # only in configurations intended for debugging.
    set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT
        "$<$<CONFIG:Debug,RelWithDebInfo>:Embedded>")

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
        -Wno-missing-designated-field-initializers
    )
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    add_compile_options(-Wall -Wextra)
endif()

# ==============================================================================
# Release code-generation profile
# ==============================================================================
# Keep distributed SDK objects portable by default. Performance deployments and
# reproducible benchmark builds can select a declared CPU contract instead of
# relying on the build host's accidental `-march=native` feature set.
set(CNETMOD_CPU_TARGET "portable" CACHE STRING
    "Release CPU target: portable, x86-64-v3, or native")
set_property(CACHE CNETMOD_CPU_TARGET PROPERTY STRINGS portable x86-64-v3 native)
option(CNETMOD_ENABLE_THIN_LTO
    "Enable ThinLTO/IPO for optimized cnetmod targets" OFF)

if(NOT CNETMOD_CPU_TARGET MATCHES "^(portable|x86-64-v3|native)$")
    message(FATAL_ERROR
        "CNETMOD_CPU_TARGET must be portable, x86-64-v3, or native")
endif()

function(cnetmod_enable_release_codegen target)
    if(NOT TARGET ${target})
        return()
    endif()
    if(CNETMOD_CPU_TARGET STREQUAL "x86-64-v3")
        if(MSVC)
            message(FATAL_ERROR
                "CNETMOD_CPU_TARGET=x86-64-v3 requires Clang or GCC")
        elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
            target_compile_options(${target} PRIVATE
                "$<$<CONFIG:Release,RelWithDebInfo>:-march=x86-64-v3>")
            if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
                # Clang 22 may widen aggregate initialization inside a
                # coroutine frame to an aligned 256-bit store even though the
                # frame returned by the ordinary allocation function is only
                # max_align_t-aligned. Prefer 128-bit vectors until LLVM makes
                # that lowering alignment-safe; scalar AVX2 instructions and
                # the rest of the x86-64-v3 contract remain available.
                target_compile_options(${target} PRIVATE
                    "$<$<CONFIG:Release,RelWithDebInfo>:-mprefer-vector-width=128>")
            endif()
        else()
            message(FATAL_ERROR
                "x86-64-v3 was selected for a non-x86_64 target")
        endif()
    elseif(CNETMOD_CPU_TARGET STREQUAL "native")
        if(MSVC)
            target_compile_options(${target} PRIVATE
                "$<$<CONFIG:Release,RelWithDebInfo>:/arch:AVX2>")
        else()
            target_compile_options(${target} PRIVATE
                "$<$<CONFIG:Release,RelWithDebInfo>:-march=native>")
            if(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND
                CMAKE_SYSTEM_PROCESSOR MATCHES "^(x86_64|AMD64|amd64)$")
                target_compile_options(${target} PRIVATE
                    "$<$<CONFIG:Release,RelWithDebInfo>:-mprefer-vector-width=128>")
            endif()
        endif()
    endif()

    if(CNETMOD_ENABLE_THIN_LTO)
        if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
            target_compile_options(${target} PRIVATE
                "$<$<CONFIG:Release,RelWithDebInfo>:-flto=thin>")
            target_link_options(${target} PRIVATE
                "$<$<CONFIG:Release,RelWithDebInfo>:-flto=thin>")
        else()
            set_property(TARGET ${target} PROPERTY
                INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
            set_property(TARGET ${target} PROPERTY
                INTERPROCEDURAL_OPTIMIZATION_RELWITHDEBINFO TRUE)
        endif()
    endif()
endfunction()

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
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/3rdparty/ThirdPartyDependencies.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/dependencies/${CNETMOD_DEPENDENCY_MODE}.cmake)
cnetmod_configure_third_party_dependencies()

# QUIC requires BoringSSL's QUIC API (SSL_QUIC_METHOD with
# set_read_secret/set_write_secret). The protocol option alone is not enough;
# downgrade it when the BoringSSL QUIC backend is unavailable so the
# conditional QUIC sources stay excluded from the build.
if(CNETMOD_ENABLE_QUIC AND NOT CNETMOD_HAS_QUIC)
    message(WARNING
        "CNETMOD_ENABLE_QUIC is ON but BoringSSL QUIC support is unavailable "
        "(set -DCNETMOD_ENABLE_BORINGSSL_QUIC=ON). Disabling QUIC protocol.")
    set(CNETMOD_ENABLE_QUIC OFF)
endif()

# ==============================================================================
# Platform-specific I/O Backend Detection
# ==============================================================================
option(CNETMOD_USE_LIBKQUEUE
    "Use the libkqueue compatibility backend on non-Apple Unix systems"
    OFF)

if(WIN32)
    set(CNETMOD_HAS_IOCP ON)
    message(STATUS "I/O backend: IOCP (Windows)")
elseif(APPLE)
    set(CNETMOD_HAS_KQUEUE ON)
    message(STATUS "I/O backend: kqueue (macOS)")
elseif(UNIX AND CNETMOD_USE_LIBKQUEUE)
    find_path(CNETMOD_LIBKQUEUE_INCLUDE_DIR
        NAMES sys/event.h
        PATH_SUFFIXES kqueue
        REQUIRED)
    find_library(CNETMOD_LIBKQUEUE_LIBRARY
        NAMES kqueue
        REQUIRED)
    set(CNETMOD_HAS_KQUEUE ON)
    message(STATUS "I/O backend: libkqueue compatibility layer (Unix)")
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
list(REMOVE_ITEM CPP_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/src/component_facade.cpp")
# query_wrapper<T> is instantiated for application-defined models. Its member
# definitions are included by the module interface; keep the BMI dependency
# explicit because CMake's module scanner does not track textual includes.
set_source_files_properties(
    "${CMAKE_CURRENT_SOURCE_DIR}/src/database/orm/query_wrapper.cppm"
    PROPERTIES
        OBJECT_DEPENDS
            "${CMAKE_CURRENT_SOURCE_DIR}/src/database/orm/query_wrapper_impl.inc")

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

if(NOT CNETMOD_ENABLE_OPENAI)
    list(FILTER MODULE_INTERFACE_FILES EXCLUDE REGEX
        "[/\\\\]src[/\\\\]ai\\.(cppm|ixx|cpp)$")
endif()

# The application host and OTLP adapters are HTTP-based composition features.
# Keep a protocol-free core build valid when HTTP is intentionally disabled.
if(NOT CNETMOD_ENABLE_HTTP)
    foreach(source_list_variable IN ITEMS MODULE_INTERFACE_FILES
            MODULE_IMPLEMENTATION_CPP_FILES REGULAR_CPP_SOURCES)
        # Neutral monitoring contracts remain available to protocol-only users.
        # Their physical application directory does not introduce Host/HTTP imports.
        set(monitoring_core_sources "${${source_list_variable}}")
        list(FILTER monitoring_core_sources INCLUDE REGEX
            "[/\\]src[/\\]application[/\\]monitoring[/\\]core[/\\]")
        list(FILTER ${source_list_variable} EXCLUDE REGEX
            "[/\\]src[/\\]application([/\\]|\\.cppm$)")
        list(APPEND ${source_list_variable} ${monitoring_core_sources})
    endforeach()
endif()

# YAML configuration is an Application-only extension.  Preserve the JSON-only
# build when consumers explicitly turn it off, including its module interface.
if(NOT CNETMOD_HAS_YAML_CONFIGURATION)
    foreach(source_list_variable IN ITEMS MODULE_INTERFACE_FILES
            MODULE_IMPLEMENTATION_CPP_FILES REGULAR_CPP_SOURCES)
        list(FILTER ${source_list_variable} EXCLUDE REGEX
            "[/\\]src[/\\]application[/\\]yaml_configuration\\.(cppm|cpp)$")
    endforeach()
endif()

# JWT signing depends on the TLS/crypto provider. Keep SSL-free protocol-only
# probes free of both the implementation unit and its module interface.
if(NOT CNETMOD_ENABLE_SSL)
    # HMAC is an OpenSSL-backed optional primitive, not a protocol-free utility.
    # Exclude both halves so SSL-free consumers never see a dangling interface.
    foreach(source_list_variable IN ITEMS MODULE_INTERFACE_FILES
            MODULE_IMPLEMENTATION_CPP_FILES REGULAR_CPP_SOURCES)
        list(FILTER ${source_list_variable} EXCLUDE REGEX
            "[/\\]utils[/\\]hmac_sha256\\.(cppm|ixx|cpp)$")
    endforeach()
    list(FILTER MODULE_INTERFACE_FILES EXCLUDE REGEX
        "[/\\]security(\\.cppm|[/\\].*\\.(cppm|ixx))$")
    list(FILTER MODULE_IMPLEMENTATION_CPP_FILES EXCLUDE REGEX
        "[/\\]security[/\\].*\\.cpp$")
    list(FILTER REGULAR_CPP_SOURCES EXCLUDE REGEX
        "[/\\]security[/\\].*\\.cpp$")
endif()

# HTTP/3 modules live under src/protocol/http/v3/ and depend on QUIC primitives
# (e.g. http3_frame imports :semantics and ::varint).  CMake's file(STRINGS)
# scanner detects "export module" regardless of preprocessor conditions, so when
# QUIC is disabled these files are still misclassified as module interfaces and
# break dyndep generation.  Exclude the entire http/v3/ directory.
if(NOT CNETMOD_ENABLE_QUIC)
    list(FILTER MODULE_INTERFACE_FILES EXCLUDE REGEX "http/v3/.*\\.cppm$")
    list(FILTER MODULE_IMPLEMENTATION_CPP_FILES EXCLUDE REGEX "http/v3/.*\\.cpp$")
    list(FILTER REGULAR_CPP_SOURCES EXCLUDE REGEX "http/v3/.*\\.cpp$")

    # Also exclude entire QUIC protocol stack when QUIC is disabled
    # (some .cppm files have conditional module declarations that become invalid)
    list(FILTER MODULE_INTERFACE_FILES EXCLUDE REGEX "quic/.*\\.cppm$")
    list(FILTER MODULE_IMPLEMENTATION_CPP_FILES EXCLUDE REGEX "quic/.*\\.cpp$")
    list(FILTER REGULAR_CPP_SOURCES EXCLUDE REGEX "quic/.*\\.cpp$")
endif()

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

# QUIC partition implementation units were converted to implementation units of
# the primary module `cnetmod.protocol.quic` (see src/protocol/quic/*.cpp), so
# they can remain in the regular source list: partition implementation units
# would have to live in a CXX_MODULES FILE_SET and introduce an implicit
# dependency cycle with the primary interface unit.
# Keep the SSL and DTLS module units in non-OpenSSL builds.  Their declarations
# and implementations are already guarded by CNETMOD_HAS_SSL, so they become
# empty compatibility modules without introducing an OpenSSL link dependency.
# Other protocol modules can then import their stable module names while
# compiling their own TLS code out.  Removing the units here made MSVC fail
# dependency scanning with C2230 before those feature guards were considered.

# ==============================================================================
# Component libraries
# ==============================================================================

# Keep each public C++ module and its implementation in the component that owns
# it.  cnetmod_core remains a tiny static compatibility facade; it no longer
# contains every enabled protocol in one physical archive.
macro(cnetmod_extract_component PREFIX SOURCE_REGEX)
    foreach(_kind MODULE_INTERFACE_FILES MODULE_IMPLEMENTATION_CPP_FILES
            REGULAR_CPP_SOURCES)
        set(${PREFIX}_${_kind} "${${_kind}}")
        list(FILTER ${PREFIX}_${_kind} INCLUDE REGEX "${SOURCE_REGEX}")
        list(FILTER ${_kind} EXCLUDE REGEX "${SOURCE_REGEX}")
    endforeach()
endmacro()

# Monitoring contracts are protocol-neutral despite their physical location
# below application/.  Keep them in runtime so protocol-only consumers retain
# metrics and tracing without linking the Application composition layer.
cnetmod_extract_component(CNETMOD_MONITORING_CORE
    "[/\\\\]src[/\\\\]application[/\\\\]monitoring[/\\\\]core[/\\\\]")
cnetmod_extract_component(CNETMOD_HTTP_REDIS_CACHE
    "[/\\\\]src[/\\\\]protocol[/\\\\]http[/\\\\]middleware[/\\\\]redis_cache\\.(cppm|ixx|cpp)$")
cnetmod_extract_component(CNETMOD_ORM
    "[/\\\\]src[/\\\\]database[/\\\\]orm[/\\\\]")

set(CNETMOD_PROTOCOL_COMPONENT_TARGETS)
foreach(protocol IN LISTS CNETMOD_PROTOCOLS)
    if(NOT CNETMOD_ENABLE_${protocol})
        continue()
    endif()
    string(TOLOWER "${protocol}" _protocol_lower)
    set(_protocol_directory "${CNETMOD_PROTOCOL_${protocol}_DIRECTORY}")
    cnetmod_extract_component(CNETMOD_PROTOCOL_${protocol}
        "[/\\\\]src[/\\\\]protocol[/\\\\]${_protocol_directory}([/\\\\]|\\.(cppm|ixx|cpp)$)")
    list(APPEND CNETMOD_PROTOCOL_COMPONENT_TARGETS
        "cnetmod_${_protocol_lower}")
endforeach()

cnetmod_extract_component(CNETMOD_APPLICATION
    "[/\\\\]src[/\\\\](application([/\\\\]|\\.(cppm|ixx|cpp)$)|ai\\.(cppm|ixx|cpp)$)")

# Repository composition is an optional Application capability.  Do not leave
# its module interfaces in the Application target when ORM is disabled: an
# empty/preprocessor-guarded facade is insufficient because
# application.orm_repository itself imports concrete ORM modules during CMake's
# dependency scan.
if(NOT CNETMOD_ENABLE_ORM)
    foreach(_kind MODULE_INTERFACE_FILES MODULE_IMPLEMENTATION_CPP_FILES
            REGULAR_CPP_SOURCES)
        list(FILTER CNETMOD_APPLICATION_${_kind} EXCLUDE REGEX
            "[/\\\\]src[/\\\\]application[/\\\\](data|orm_repository)\\.(cppm|ixx|cpp)$")
        list(FILTER CNETMOD_APPLICATION_${_kind} EXCLUDE REGEX
            "[/\\\\]src[/\\\\]application[/\\\\]integration[/\\\\](mysql_orm|postgresql_orm|clickhouse_orm)\\.(cppm|ixx|cpp)$")
    endforeach()
endif()

set(CNETMOD_RUNTIME_MODULE_INTERFACE_FILES
    ${MODULE_INTERFACE_FILES}
    ${CNETMOD_MONITORING_CORE_MODULE_INTERFACE_FILES})
set(CNETMOD_RUNTIME_MODULE_IMPLEMENTATION_CPP_FILES
    ${MODULE_IMPLEMENTATION_CPP_FILES}
    ${CNETMOD_MONITORING_CORE_MODULE_IMPLEMENTATION_CPP_FILES})
set(CNETMOD_RUNTIME_REGULAR_CPP_SOURCES
    ${REGULAR_CPP_SOURCES}
    ${CNETMOD_MONITORING_CORE_REGULAR_CPP_SOURCES})

function(cnetmod_configure_component TARGET_NAME EXPORT_NAME)
    target_include_directories(${TARGET_NAME} PUBLIC
        $<BUILD_INTERFACE:${CNETMOD_GENERATED_INCLUDE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
    target_compile_features(${TARGET_NAME} PUBLIC cxx_std_23)
    if(MSVC)
        # A consumer recompiles installed module interfaces to produce local
        # BMIs.  Keep the character-set environment identical to the archive
        # build or MSVC reports C5050 module incompatibility warnings.
        target_compile_options(${TARGET_NAME} PUBLIC /utf-8)
    endif()
    set_target_properties(${TARGET_NAME} PROPERTIES
        CXX_MODULE_GENERATION_MODE "SEPARATE"
        POSITION_INDEPENDENT_CODE ON
        EXPORT_NAME "${EXPORT_NAME}")
endfunction()

function(cnetmod_add_component TARGET_NAME EXPORT_NAME INTERFACE_VAR
        IMPLEMENTATION_VAR REGULAR_VAR)
    add_library(${TARGET_NAME} STATIC
        ${${IMPLEMENTATION_VAR}}
        ${${REGULAR_VAR}})
    cnetmod_configure_component(${TARGET_NAME} ${EXPORT_NAME})
    target_link_libraries(${TARGET_NAME} PUBLIC cnetmod_runtime)
    if(${INTERFACE_VAR})
        target_sources(${TARGET_NAME} PUBLIC
            FILE_SET cxx_modules TYPE CXX_MODULES
            BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
            FILES ${${INTERFACE_VAR}})
    endif()
endfunction()

add_library(cnetmod_runtime STATIC
    ${CNETMOD_RUNTIME_REGULAR_CPP_SOURCES}
    ${CNETMOD_RUNTIME_MODULE_IMPLEMENTATION_CPP_FILES})
cnetmod_configure_component(cnetmod_runtime runtime)
add_library(cnetmod::runtime ALIAS cnetmod_runtime)

target_include_directories(cnetmod_runtime PUBLIC
    $<BUILD_INTERFACE:${CNETMOD_GENERATED_INCLUDE_DIR}>
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

# Platform-specific macro definitions
target_compile_definitions(cnetmod_runtime PUBLIC
    $<$<PLATFORM_ID:Windows>:CNETMOD_PLATFORM_WINDOWS>
    $<$<PLATFORM_ID:Linux>:CNETMOD_PLATFORM_LINUX>
    $<$<PLATFORM_ID:Darwin>:CNETMOD_PLATFORM_MACOS>
    $<$<BOOL:${CNETMOD_HAS_IOCP}>:CNETMOD_HAS_IOCP>
    $<$<BOOL:${CNETMOD_HAS_IO_URING}>:CNETMOD_HAS_IO_URING>
    $<$<BOOL:${CNETMOD_HAS_IO_URING_BUFFER_RING}>:CNETMOD_HAS_IO_URING_BUFFER_RING>
    $<$<BOOL:${CNETMOD_HAS_EPOLL}>:CNETMOD_HAS_EPOLL>
    $<$<BOOL:${CNETMOD_HAS_KQUEUE}>:CNETMOD_HAS_KQUEUE>
    $<$<BOOL:${CNETMOD_USE_LIBKQUEUE}>:CNETMOD_DISABLE_EPOLL>
    $<$<BOOL:${CNETMOD_HAS_SSL}>:CNETMOD_HAS_SSL>
    $<$<BOOL:${BoringSSL_FOUND}>:CNETMOD_USING_BORINGSSL>
    $<$<BOOL:${CNETMOD_HAS_QUIC}>:CNETMOD_HAS_SSL>
    $<$<BOOL:${CNETMOD_HAS_QUIC}>:CNETMOD_HAS_QUIC>
    $<$<BOOL:${CNETMOD_ENABLE_QUIC}>:CNETMOD_ENABLE_QUIC>
    $<$<BOOL:${CNETMOD_HAS_ZLIB}>:CNETMOD_HAS_ZLIB>
    $<$<BOOL:${CNETMOD_HAS_ZSTD}>:CNETMOD_HAS_ZSTD>
    $<$<BOOL:${CNETMOD_HAS_SNAPPY}>:CNETMOD_HAS_SNAPPY>
    $<$<BOOL:${CNETMOD_HAS_BROTLI}>:CNETMOD_HAS_BROTLI>
    $<$<BOOL:${CNETMOD_HAS_LZ4}>:CNETMOD_HAS_LZ4>
    $<$<BOOL:${CNETMOD_HAS_LEVELDB}>:CNETMOD_HAS_LEVELDB>
    $<$<BOOL:${CNETMOD_HAS_ICU}>:CNETMOD_HAS_ICU>
    $<$<BOOL:${CNETMOD_HAS_YAML_CONFIGURATION}>:CNETMOD_HAS_YAML_CONFIGURATION>
    $<$<BOOL:${CNETMOD_ENABLE_ORM}>:CNETMOD_HAS_ORM>
    $<$<BOOL:${CNETMOD_ENABLE_OPENAI}>:CNETMOD_HAS_CHAT_MODEL>
)
foreach(protocol IN LISTS CNETMOD_PROTOCOLS)
    if(CNETMOD_ENABLE_${protocol})
        target_compile_definitions(cnetmod_runtime PUBLIC
            CNETMOD_HAS_PROTOCOL_${protocol})
    endif()
endforeach()

# Platform-specific linking
if(WIN32)
    target_link_libraries(cnetmod_runtime PUBLIC ws2_32 mswsock crypt32)
elseif(CNETMOD_USE_LIBKQUEUE)
    target_include_directories(cnetmod_runtime PRIVATE
        "${CNETMOD_LIBKQUEUE_INCLUDE_DIR}")
    target_link_libraries(cnetmod_runtime PRIVATE
        "${CNETMOD_LIBKQUEUE_LIBRARY}")
elseif(CNETMOD_HAS_IO_URING)
    target_link_libraries(cnetmod_runtime PUBLIC uring)
endif()

cnetmod_link_stdexec(cnetmod_runtime)
cnetmod_link_nlohmann_json(cnetmod_runtime)
cnetmod_link_boringssl(cnetmod_runtime)
cnetmod_link_selected_stdlib(cnetmod_runtime)
configure_cxx_modules(
    TARGET cnetmod_runtime
    MODULE_INTERFACE_FILES ${CNETMOD_RUNTIME_MODULE_INTERFACE_FILES}
    DEPENDENCY_MODULE_INTERFACE_FILES ${CNETMOD_NLOHMANN_JSON_MODULE})

set(CNETMOD_COMPONENT_TARGETS cnetmod_runtime)

if(CNETMOD_ENABLE_ORM)
    cnetmod_add_component(cnetmod_orm orm
        CNETMOD_ORM_MODULE_INTERFACE_FILES
        CNETMOD_ORM_MODULE_IMPLEMENTATION_CPP_FILES
        CNETMOD_ORM_REGULAR_CPP_SOURCES)
    add_library(cnetmod::orm ALIAS cnetmod_orm)
    cnetmod_link_pugixml(cnetmod_orm)
    list(APPEND CNETMOD_COMPONENT_TARGETS cnetmod_orm)
endif()

foreach(protocol IN LISTS CNETMOD_PROTOCOLS)
    if(NOT CNETMOD_ENABLE_${protocol})
        continue()
    endif()
    string(TOLOWER "${protocol}" _protocol_lower)
    set(_target "cnetmod_${_protocol_lower}")
    cnetmod_add_component(${_target} ${_protocol_lower}
        CNETMOD_PROTOCOL_${protocol}_MODULE_INTERFACE_FILES
        CNETMOD_PROTOCOL_${protocol}_MODULE_IMPLEMENTATION_CPP_FILES
        CNETMOD_PROTOCOL_${protocol}_REGULAR_CPP_SOURCES)
    add_library(cnetmod::${_protocol_lower} ALIAS ${_target})
    list(APPEND CNETMOD_COMPONENT_TARGETS ${_target})
endforeach()

# Third-party implementations belong to the component that uses them.  This
# keeps a runtime-only consumer from inheriting every optional protocol SDK.
if(TARGET cnetmod_http)
    cnetmod_link_zlib(cnetmod_http)
    cnetmod_link_brotli(cnetmod_http)
endif()
if(TARGET cnetmod_grpc)
    cnetmod_link_zlib(cnetmod_grpc)
    cnetmod_link_zstd(cnetmod_grpc)
    cnetmod_link_brotli(cnetmod_grpc)
endif()
if(TARGET cnetmod_kafka)
    cnetmod_link_zlib(cnetmod_kafka)
    cnetmod_link_zstd(cnetmod_kafka)
    cnetmod_link_lz4(cnetmod_kafka)
endif()
if(TARGET cnetmod_clickhouse)
    if(NOT CNETMOD_HAS_LZ4 OR NOT CNETMOD_HAS_ZSTD)
        message(FATAL_ERROR "ClickHouse Native requires LZ4 and Zstandard libraries")
    endif()
    target_sources(cnetmod_clickhouse PRIVATE 3rdparty/cityhash102/src/city.cc)
    target_include_directories(cnetmod_clickhouse PRIVATE 3rdparty/cityhash102/include)
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/cityhash102/LICENSE" DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/cnetmod/cityhash102")
    cnetmod_link_lz4(cnetmod_clickhouse)
    cnetmod_link_zstd(cnetmod_clickhouse)
endif()
if(TARGET cnetmod_mongodb)
    if(WIN32)
        target_link_libraries(cnetmod_mongodb PUBLIC dnsapi)
    elseif(UNIX)
        target_link_libraries(cnetmod_mongodb PUBLIC resolv)
    endif()
    cnetmod_link_snappy(cnetmod_mongodb)
    cnetmod_link_zlib(cnetmod_mongodb)
    cnetmod_link_zstd(cnetmod_mongodb)
    cnetmod_link_icu(cnetmod_mongodb)
endif()
if(TARGET cnetmod_raft)
    cnetmod_link_leveldb(cnetmod_raft)
endif()
if(TARGET cnetmod_postgresql)
    cnetmod_link_icu(cnetmod_postgresql)
endif()

# Cross-protocol imports form a directed component graph.  HTTP owns optional
# HTTP/3 support, while the Redis-backed HTTP cache remains an explicit
# integration component below.
foreach(_edge IN ITEMS
        "dns;http"
        "grpc;http"
        "mqtt;http"
        "mqtt;websocket"
        "openai;http"
        "websocket;http")
    list(GET _edge 0 _consumer)
    list(GET _edge 1 _dependency)
    if(TARGET cnetmod_${_consumer} AND TARGET cnetmod_${_dependency})
        target_link_libraries(cnetmod_${_consumer} PUBLIC
            cnetmod_${_dependency})
    endif()
endforeach()
if(TARGET cnetmod_openai AND TARGET cnetmod_socks5)
    target_link_libraries(cnetmod_openai PUBLIC cnetmod_socks5)
endif()
if(TARGET cnetmod_http AND TARGET cnetmod_quic)
    target_link_libraries(cnetmod_http PUBLIC cnetmod_quic)
endif()
if(CNETMOD_ENABLE_ORM)
    foreach(_database_target IN ITEMS cnetmod_mysql cnetmod_postgresql cnetmod_clickhouse)
        if(TARGET ${_database_target})
            target_link_libraries(${_database_target} PUBLIC cnetmod_orm)
        endif()
    endforeach()
endif()

if(TARGET cnetmod_http AND TARGET cnetmod_redis)
    cnetmod_add_component(cnetmod_http_redis_cache http_redis_cache
        CNETMOD_HTTP_REDIS_CACHE_MODULE_INTERFACE_FILES
        CNETMOD_HTTP_REDIS_CACHE_MODULE_IMPLEMENTATION_CPP_FILES
        CNETMOD_HTTP_REDIS_CACHE_REGULAR_CPP_SOURCES)
    target_link_libraries(cnetmod_http_redis_cache PUBLIC
        cnetmod_http cnetmod_redis)
    add_library(cnetmod::http_redis_cache ALIAS cnetmod_http_redis_cache)
    list(APPEND CNETMOD_COMPONENT_TARGETS cnetmod_http_redis_cache)
endif()

if(CNETMOD_APPLICATION_MODULE_INTERFACE_FILES OR
        CNETMOD_APPLICATION_MODULE_IMPLEMENTATION_CPP_FILES OR
        CNETMOD_APPLICATION_REGULAR_CPP_SOURCES)
    cnetmod_add_component(cnetmod_application application
        CNETMOD_APPLICATION_MODULE_INTERFACE_FILES
        CNETMOD_APPLICATION_MODULE_IMPLEMENTATION_CPP_FILES
        CNETMOD_APPLICATION_REGULAR_CPP_SOURCES)
    add_library(cnetmod::application ALIAS cnetmod_application)
    if(TARGET cnetmod_orm)
        target_link_libraries(cnetmod_application PUBLIC cnetmod_orm)
    endif()
    foreach(_protocol_target IN LISTS CNETMOD_PROTOCOL_COMPONENT_TARGETS)
        target_link_libraries(cnetmod_application PUBLIC ${_protocol_target})
    endforeach()
    if(TARGET cnetmod_mongodb)
        target_link_libraries(cnetmod_application PRIVATE ${CNETMOD_PUGIXML_TARGET})
    endif()
    if(CNETMOD_HAS_YAML_CONFIGURATION)
        target_link_libraries(cnetmod_application PRIVATE yaml_cpp::yaml_cpp)
    endif()
    list(APPEND CNETMOD_COMPONENT_TARGETS cnetmod_application)
endif()

# Compatibility/convenience facade.  Its own archive contains only a marker;
# the linked component targets retain the actual implementation and module
# ownership.  Keeping a physical target preserves `--target cnetmod_core` for
# existing build scripts without recreating the monolith.
add_library(cnetmod_core STATIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src/component_facade.cpp")
cnetmod_configure_component(cnetmod_core cnetmod_core)
target_link_libraries(cnetmod_core PUBLIC ${CNETMOD_COMPONENT_TARGETS})
foreach(_cnetmod_component IN LISTS CNETMOD_COMPONENT_TARGETS)
    cnetmod_enable_release_codegen(${_cnetmod_component})
endforeach()
cnetmod_enable_release_codegen(cnetmod_core)
add_library(cnetmod::core ALIAS cnetmod_core)
add_library(cnetmod::all ALIAS cnetmod_core)


# ==============================================================================
# C ABI bindings
# ==============================================================================
# The C ABI is deliberately a separate library: Rust/Python native extensions
# can link it without exposing compiler-specific C++ module BMIs. Keep it
# static by default: the C++ module core owns process-level initialization and
# is safely embedded in the extension/application that owns its event loop.
option(CNETMOD_BUILD_C_API "Build the stable C ABI library" OFF)
if(CNETMOD_BUILD_C_API AND CNETMOD_ENABLE_HTTP)
    set_target_properties(cnetmod_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
    add_library(cnetmod_c STATIC
        "${CMAKE_CURRENT_SOURCE_DIR}/bindings/c_api/cnetmod_c_api.cpp")
    # The CPython extension embeds this archive in a shared object on ELF
    # platforms, so its objects must be position independent as well.
    set_target_properties(cnetmod_c PROPERTIES POSITION_INDEPENDENT_CODE ON)
    target_link_libraries(cnetmod_c PRIVATE cnetmod_core)
    target_compile_features(cnetmod_c PRIVATE cxx_std_23)
    target_compile_definitions(cnetmod_c PUBLIC CNETMOD_C_API_STATIC)
    target_include_directories(cnetmod_c PUBLIC
        $<BUILD_INTERFACE:${CNETMOD_GENERATED_INCLUDE_DIR}>
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
    set_property(TARGET cnetmod_c PROPERTY
        CXX_MODULE_GENERATION_MODE "SEPARATE")
endif()
if(CNETMOD_BUILD_C_API AND NOT CNETMOD_ENABLE_HTTP)
    message(STATUS "C API requires HTTP; skipping cnetmod_c because CNETMOD_ENABLE_HTTP=OFF")
endif()

# CPython extension built on the stable C ABI. It deliberately links the
# static cnetmod_c target into the extension, so Python imports no C++ module
# DLL and owns the same event-loop lifetime as the binding.
option(CNETMOD_BUILD_PYTHON_BINDINGS "Build the CPython native extension" OFF)
if(TARGET cnetmod_c AND CNETMOD_BUILD_PYTHON_BINDINGS)
    find_package(Python3 COMPONENTS Interpreter Development.SABIModule QUIET)
    if(Python3_Development.SABIModule_FOUND)
        Python3_add_library(cnetmod_python MODULE USE_SABI 3.11 WITH_SOABI
            "${CMAKE_CURRENT_SOURCE_DIR}/bindings/python/cnetmod_native.cpp")
        target_link_libraries(cnetmod_python PRIVATE cnetmod_c)
        target_compile_features(cnetmod_python PRIVATE cxx_std_23)
        target_include_directories(cnetmod_python PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}/include")
        set_target_properties(cnetmod_python PROPERTIES PREFIX ""
            OUTPUT_NAME "cnetmod_native")
        if(WIN32)
            set_target_properties(cnetmod_python PROPERTIES SUFFIX ".pyd")
        endif()
    else()
        message(STATUS "CPython stable-ABI development files not found; skipping cnetmod_python")
    endif()
endif()

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
if(CNETMOD_HAS_PUGIXML AND CNETMOD_PUGIXML_TARGET STREQUAL "pugixml-static")
    # pugixml is built from the bundled source when no system package is
    # selected.  Exporting cnetmod_core without installing this archive leaves
    # every downstream Release consumer with a dangling imported target.
    install(TARGETS pugixml-static
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    )
endif()
if(CNETMOD_HAS_LZ4 AND CNETMOD_LZ4_TARGET STREQUAL "lz4_static")
    install(TARGETS lz4_static
        EXPORT cnetmodTargets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
    install(FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/lz4/lib/lz4.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/lz4/lib/lz4hc.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/lz4/lib/lz4frame.h"
        "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/lz4/lib/lz4file.h"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/lz4/LICENSE"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/cnetmod/lz4")
endif()
if(CNETMOD_HAS_ZSTD AND CNETMOD_ZSTD_TARGET STREQUAL "libzstd_static")
    target_include_directories(libzstd_static INTERFACE
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
    install(TARGETS libzstd_static
        EXPORT cnetmodTargets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}")
    install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/zstd/LICENSE"
        DESTINATION "${CMAKE_INSTALL_DATADIR}/licenses/cnetmod/zstd")
endif()
if(BoringSSL_FOUND AND TARGET ssl AND TARGET crypto)
    install(TARGETS ssl crypto
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}/cnetmod/boringssl"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}/cnetmod/boringssl")
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/boringssl/include/"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
endif()
if(CNETMOD_USING_BUNDLED_ICU)
    # Ship the exact ICU build used by the PostgreSQL component.  Consumers of
    # an installed SDK must never fall back to an unrelated machine-wide ICU.
    install(FILES
        "$<TARGET_LINKER_FILE:ICU::uc>"
        "$<TARGET_LINKER_FILE:ICU::i18n>"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cnetmod/icu")
    install(FILES
        "$<TARGET_FILE:ICU::uc>"
        "$<TARGET_FILE:ICU::i18n>"
        "${CNETMOD_BUNDLED_ICU_RUNTIME_DIR}/icudt78.dll"
        DESTINATION "${CMAKE_INSTALL_BINDIR}")
    install(DIRECTORY
        "${CNETMOD_BUNDLED_ICU_SOURCE_DIR}/common/unicode"
        "${CNETMOD_BUNDLED_ICU_SOURCE_DIR}/i18n/unicode"
        DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
endif()
install(TARGETS ${CNETMOD_COMPONENT_TARGETS}
    EXPORT cnetmodTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
    FILE_SET cxx_modules DESTINATION "${CMAKE_INSTALL_LIBDIR}/cnetmod/modules"
)
install(TARGETS cnetmod_core
    EXPORT cnetmodTargets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
if(UNIX AND STDLIB_MODULE_DIRS)
    # libc++'s std module units include generated declaration fragments from
    # adjacent std/ and std.compat/ directories.  CMake installs only files
    # listed in the module file set, so preserve those required companions for
    # consumers that rebuild the installed module graph.
    install(DIRECTORY
        "${STDLIB_MODULE_DIRS}/std"
        "${STDLIB_MODULE_DIRS}/std.compat"
        DESTINATION "${CMAKE_INSTALL_LIBDIR}/cnetmod/modules")
endif()
# Public module interfaces may include template implementation fragments that
# are intentionally not standalone module units. Preserve their source-relative
# paths so installed consumers can rebuild the exported module graph.
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/src/"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cnetmod/modules/src"
    FILES_MATCHING
        PATTERN "*.inc"
        PATTERN "*.inl"
        PATTERN "*.ipp"
        PATTERN "*.tpp")
if(TARGET cnetmod_c)
    install(TARGETS cnetmod_c
        EXPORT cnetmodTargets
        ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
        RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
endif()
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/include/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")
install(FILES "${CNETMOD_GENERATED_INCLUDE_DIR}/cnetmod/version.hpp"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}/cnetmod")
install(DIRECTORY "${CNETMOD_NLOHMANN_JSON_SOURCE_DIR}/include/nlohmann"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}")

# stdexec remains a private build-time implementation dependency.  It is not
# installed because no public cnetmod module interface exposes its types.

set(CNETMOD_PACKAGE_COMPONENTS core coro io executor json security)
if(TARGET cnetmod_application)
    list(APPEND CNETMOD_PACKAGE_COMPONENTS application)
endif()
if(TARGET cnetmod_orm)
    list(APPEND CNETMOD_PACKAGE_COMPONENTS orm)
endif()
foreach(protocol IN LISTS CNETMOD_PROTOCOLS)
    if(CNETMOD_ENABLE_${protocol})
        string(TOLOWER "${protocol}" protocol_component)
        list(APPEND CNETMOD_PACKAGE_COMPONENTS "${protocol_component}")
    endif()
endforeach()
if(TARGET cnetmod_http_redis_cache)
    list(APPEND CNETMOD_PACKAGE_COMPONENTS http_redis_cache)
endif()
list(REMOVE_DUPLICATES CNETMOD_PACKAGE_COMPONENTS)

set(CNETMOD_PACKAGE_COMPONENTS_JSON "")
foreach(component IN LISTS CNETMOD_PACKAGE_COMPONENTS)
    if(CNETMOD_PACKAGE_COMPONENTS_JSON)
        string(APPEND CNETMOD_PACKAGE_COMPONENTS_JSON ", ")
    endif()
    string(APPEND CNETMOD_PACKAGE_COMPONENTS_JSON "\"${component}\"")
endforeach()

if(MSVC)
    set(CNETMOD_PACKAGE_STDLIB "msvc")
    set(CNETMOD_PACKAGE_RUNTIME "md")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    set(CNETMOD_PACKAGE_STDLIB "libc++")
    set(CNETMOD_PACKAGE_RUNTIME "native")
else()
    set(CNETMOD_PACKAGE_STDLIB "libstdc++")
    set(CNETMOD_PACKAGE_RUNTIME "native")
endif()
set(CNETMOD_DISTRIBUTION_PLATFORM_BASELINE "native" CACHE STRING
    "Minimum platform ABI recorded in the installed SDK manifest")
if(CMAKE_CONFIGURATION_TYPES)
    set(_cnetmod_default_distribution_configuration "multi-config")
else()
    set(_cnetmod_default_distribution_configuration "${CMAKE_BUILD_TYPE}")
endif()
set(CNETMOD_DISTRIBUTION_CONFIGURATION
    "${_cnetmod_default_distribution_configuration}" CACHE STRING
    "Configuration recorded in the installed SDK manifest")
if(CNETMOD_DISTRIBUTION_CONFIGURATION STREQUAL "")
    set(CNETMOD_DISTRIBUTION_CONFIGURATION "unknown")
endif()
string(TOLOWER "${CMAKE_SYSTEM_NAME}" CNETMOD_PACKAGE_SYSTEM)
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" CNETMOD_PACKAGE_PROCESSOR)
string(TOLOWER "${CMAKE_CXX_COMPILER_ID}" CNETMOD_PACKAGE_COMPILER)
set(CNETMOD_PACKAGE_ABI
    "${CNETMOD_PACKAGE_SYSTEM}-${CNETMOD_PACKAGE_PROCESSOR}-${CNETMOD_PACKAGE_COMPILER}${CMAKE_CXX_COMPILER_VERSION}-${CNETMOD_PACKAGE_STDLIB}-${CNETMOD_PACKAGE_RUNTIME}-${CNETMOD_DISTRIBUTION_PLATFORM_BASELINE}")
string(REGEX REPLACE "[^A-Za-z0-9.+_-]" "-" CNETMOD_PACKAGE_ABI
    "${CNETMOD_PACKAGE_ABI}")
set(CNETMOD_DISTRIBUTION_TARGET_ID "${CNETMOD_PACKAGE_ABI}" CACHE STRING
    "Stable target identifier used by cnetmod release catalogs")

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cnetmod-package-manifest.json.in"
    "${CMAKE_CURRENT_BINARY_DIR}/cnetmod-package-manifest.json"
    @ONLY)
configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cnetmodConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/cnetmodConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/cnetmod"
)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cnetmodMimalloc.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/cnetmodMimalloc.cmake"
    @ONLY
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
    "${CMAKE_CURRENT_BINARY_DIR}/cnetmodMimalloc.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/cnetmod"
)
install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/cnetmod-package-manifest.json"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/cnetmod")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/cnetmod/licenses")

set(CPACK_PACKAGE_NAME "cnetmod")
set(CPACK_PACKAGE_VENDOR "banderzhm")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "cnetmod-sdk-${PROJECT_VERSION}-${CNETMOD_PACKAGE_ABI}")
set(CPACK_GENERATOR "ZIP")
include(CPack)

set(CNETMOD_LINK_TARGET cnetmod_core)

# ==============================================================================
# Main Executable
# ==============================================================================

add_executable(cnetmod_app src/main.cpp)
target_link_libraries(cnetmod_app PRIVATE ${CNETMOD_LINK_TARGET})
if(CNETMOD_USING_MIMALLOC)
    cnetmod_enable_mimalloc(cnetmod_app)
endif()
set_property(TARGET cnetmod_app PROPERTY
    CXX_MODULE_GENERATION_MODE "SEPARATE"
)

# ==============================================================================
# Testing, Benchmarks & TFB
# ==============================================================================

option(CNETMOD_BUILD_TESTS "Build tests" OFF)
option(CNETMOD_BUILD_BENCH "Build performance benchmarks" OFF)
if(CNETMOD_BUILD_TESTS)
    enable_testing()
endif()
if(CNETMOD_BUILD_TESTS AND TARGET cnetmod_python AND Python3_Interpreter_FOUND)
    add_test(NAME python_cnetmod_native_binding
        COMMAND Python3::Interpreter
            "${CMAKE_CURRENT_SOURCE_DIR}/bindings/python/test_cnetmod_native.py"
            "$<TARGET_FILE:cnetmod_python>")
    set_tests_properties(python_cnetmod_native_binding PROPERTIES TIMEOUT 45)
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/testing/CMakeLists.txt")
    add_subdirectory(testing)
endif()

# ==============================================================================
# Example Program Configuration
# ==============================================================================

option(CNETMOD_BUILD_EXAMPLES "Build examples" OFF)
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
if(TARGET cnetmod_c)
    list(APPEND CNETMOD_ALL_TARGETS cnetmod_c)
endif()
if(TARGET cnetmod_python)
    list(APPEND CNETMOD_ALL_TARGETS cnetmod_python)
endif()

list(REMOVE_DUPLICATES CNETMOD_ALL_TARGETS)

foreach(_cnetmod_target IN LISTS CNETMOD_ALL_TARGETS)
    if(NOT _cnetmod_target STREQUAL "cnetmod_core")
        cnetmod_enable_release_codegen(${_cnetmod_target})
    endif()
endforeach()

# Allocator replacement is a property of the completed process. Apply it to
# every executable owned by this build; libraries remain allocator-neutral so
# downstream applications can opt in explicitly on their final target.
if(CNETMOD_USING_MIMALLOC)
    foreach(_cnetmod_target IN LISTS CNETMOD_ALL_TARGETS)
        if(TARGET ${_cnetmod_target})
            get_target_property(_cnetmod_target_type ${_cnetmod_target} TYPE)
            get_target_property(_cnetmod_disable_mimalloc
                ${_cnetmod_target} CNETMOD_DISABLE_MIMALLOC)
            if(_cnetmod_target_type STREQUAL "EXECUTABLE" AND
                    NOT _cnetmod_disable_mimalloc)
                cnetmod_enable_mimalloc(${_cnetmod_target})
            endif()
        endif()
    endforeach()
endif()

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
        message(STATUS "  mold linker: 10-20x faster linking")
    endif()
    if(CNETMOD_USING_MIMALLOC)
        message(STATUS "  mimalloc: 2-3x faster memory allocation")
    endif()
    message(STATUS "")
endif()
