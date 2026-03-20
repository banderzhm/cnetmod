#[[
  Detect platform-specific C++ standard library module paths and
  apply required global compile/link options for the selected toolchain.

  Outputs:
    STDLIB_MODULE_DIRS
    STDLIB_INCLUDE_DIRS

  Compatibility outputs:
    LIBCXX_MODULE_DIRS
    LIBCXX_INCLUDE_DIRS
]]
function(detect_stdlib_module_paths)
    # Auto-detect platform-specific standard library module paths
    # Backward compatibility: accept legacy LIBCXX_* cache variables as input
    if(NOT STDLIB_MODULE_DIRS AND LIBCXX_MODULE_DIRS)
        set(STDLIB_MODULE_DIRS "${LIBCXX_MODULE_DIRS}")
    endif()
    if(NOT STDLIB_INCLUDE_DIRS AND LIBCXX_INCLUDE_DIRS)
        set(STDLIB_INCLUDE_DIRS "${LIBCXX_INCLUDE_DIRS}")
    endif()

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
        find_path(STDLIB_MODULE_DIRS
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
            DOC "Path to standard library modules (libc++)"
        )

        # Try to find libc++ header directory (LLVM 19-22)
        find_path(STDLIB_INCLUDE_DIRS
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

        if(STDLIB_MODULE_DIRS AND STDLIB_INCLUDE_DIRS)
            message(STATUS "Auto-detected libc++ paths:")
            message(STATUS "  Module directory: ${STDLIB_MODULE_DIRS}")
            message(STATUS "  Header directory: ${STDLIB_INCLUDE_DIRS}")

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
                get_filename_component(LLVM_LIB_PATH "${STDLIB_MODULE_DIRS}/../../../lib/c++" ABSOLUTE)
                message(STATUS "LLVM libc++ library path: ${LLVM_LIB_PATH}")

                add_compile_options(
                    -nostdinc++
                    -isystem ${STDLIB_INCLUDE_DIRS}
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
                    -isystem ${STDLIB_INCLUDE_DIRS}
                )
                add_link_options(-stdlib=libc++ -lc++ -lc++abi)
            endif()
        else()
            message(WARNING "Unable to auto-detect libc++ paths. Please set manually:")
            message(WARNING "  cmake -DSTDLIB_MODULE_DIRS=<path> -DSTDLIB_INCLUDE_DIRS=<path> ..")
        endif()

    elseif(WIN32)
        # Windows: Auto-detect MSVC standard library module paths

        # Priority 1: Try to get from VCToolsInstallDir environment variable
        if(DEFINED ENV{VCToolsInstallDir})
            set(STDLIB_MODULE_DIRS "$ENV{VCToolsInstallDir}/modules" CACHE PATH "Path to standard library modules")
            if(EXISTS "${STDLIB_MODULE_DIRS}/std.ixx")
                message(STATUS "Detected MSVC module directory from VCToolsInstallDir environment variable: ${STDLIB_MODULE_DIRS}")
            else()
                message(WARNING "Found VCToolsInstallDir but std.ixx does not exist at: ${STDLIB_MODULE_DIRS}")
                unset(STDLIB_MODULE_DIRS CACHE)
            endif()
        endif()

        # Priority 2: If environment variable not found or invalid, try to detect from compiler path
        if(NOT STDLIB_MODULE_DIRS)
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
                set(STDLIB_MODULE_DIRS "${DETECTED_MODULE_PATH}" CACHE PATH "Path to standard library modules")
                message(STATUS "Auto-detected MSVC module directory from compiler path: ${STDLIB_MODULE_DIRS}")
            else()
                # Fallback: Search common Visual Studio installation locations
                message(STATUS "Unable to detect from compiler path, searching common installation locations...")
                find_path(STDLIB_MODULE_DIRS
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

                if(STDLIB_MODULE_DIRS)
                    message(STATUS "Found MSVC module directory at common location: ${STDLIB_MODULE_DIRS}")
                else()
                    message(WARNING "Unable to auto-detect MSVC module path. Please set manually:")
                    message(WARNING "  cmake -DSTDLIB_MODULE_DIRS=<path> ..")
                endif()
            endif()
        endif()
    endif()

    # Export detected values to caller scope
    set(STDLIB_MODULE_DIRS "${STDLIB_MODULE_DIRS}" PARENT_SCOPE)
    set(STDLIB_INCLUDE_DIRS "${STDLIB_INCLUDE_DIRS}" PARENT_SCOPE)

    # Backward compatibility: also export legacy names
    set(LIBCXX_MODULE_DIRS "${STDLIB_MODULE_DIRS}" PARENT_SCOPE)
    set(LIBCXX_INCLUDE_DIRS "${STDLIB_INCLUDE_DIRS}" PARENT_SCOPE)
endfunction()

#[[
  Configure C++ module file sets for a target, including:
    - project module interfaces
    - standard library modules (if detected)
    - nlohmann/json module interface

  Args:
    TARGET <target_name>
    MODULE_INTERFACE_FILES <list...>
    JSON_MODULE <path>
]]
function(configure_cxx_modules)
    set(options)
    set(oneValueArgs TARGET JSON_MODULE)
    set(multiValueArgs MODULE_INTERFACE_FILES)
    cmake_parse_arguments(CFG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})

    if(NOT CFG_TARGET)
        message(FATAL_ERROR "configure_cxx_modules requires TARGET")
    endif()

    if(UNIX AND STDLIB_MODULE_DIRS AND STDLIB_INCLUDE_DIRS)
        # Linux/macOS: libc++ standard library modules
        target_include_directories(${CFG_TARGET} SYSTEM PUBLIC
            ${STDLIB_INCLUDE_DIRS}
        )
        target_sources(${CFG_TARGET} PUBLIC
            FILE_SET cxx_modules TYPE CXX_MODULES
            BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR} ${STDLIB_MODULE_DIRS}
            FILES
                ${CFG_MODULE_INTERFACE_FILES}
                ${STDLIB_MODULE_DIRS}/std.cppm
                ${STDLIB_MODULE_DIRS}/std.compat.cppm
                ${CFG_JSON_MODULE}
        )
    elseif(WIN32 AND STDLIB_MODULE_DIRS)
        # Windows: MSVC standard library modules
        target_sources(${CFG_TARGET} PUBLIC
            FILE_SET cxx_modules TYPE CXX_MODULES
            BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR} ${STDLIB_MODULE_DIRS}
            FILES
                ${CFG_MODULE_INTERFACE_FILES}
                ${STDLIB_MODULE_DIRS}/std.ixx
                ${STDLIB_MODULE_DIRS}/std.compat.ixx
                ${CFG_JSON_MODULE}
        )
    else()
        message(WARNING "Standard library module path not detected, please set STDLIB_MODULE_DIRS manually")
        target_sources(${CFG_TARGET} PUBLIC
            FILE_SET cxx_modules TYPE CXX_MODULES
            BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
            FILES
                ${CFG_MODULE_INTERFACE_FILES}
                ${CFG_JSON_MODULE}
        )
    endif()
endfunction()
