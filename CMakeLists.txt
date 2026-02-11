cmake_minimum_required(VERSION 3.28)

project(cnetmod
    VERSION 0.1.0
    DESCRIPTION "Cross-platform asynchronous network library with C++23 modules"
    LANGUAGES CXX
)

# ==============================================================================
# C++ 标准配置
# ==============================================================================
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ==============================================================================
# 自动检测平台特定的标准库模块路径
# ==============================================================================

if(UNIX)
    # Linux/Unix/macOS: 自动检测 libc++ 标准库模块路径
    
    # macOS 特殊处理：检测 Homebrew LLVM 路径
    if(APPLE)
        # Apple Silicon (arm64) Homebrew 路径
        set(HOMEBREW_LLVM_PREFIX_ARM64 "/opt/homebrew/opt/llvm")
        # Intel Mac (x86_64) Homebrew 路径
        set(HOMEBREW_LLVM_PREFIX_X64 "/usr/local/opt/llvm")
        
        # 根据架构选择 Homebrew 路径
        if(CMAKE_HOST_SYSTEM_PROCESSOR STREQUAL "arm64")
            set(HOMEBREW_LLVM_PREFIX ${HOMEBREW_LLVM_PREFIX_ARM64})
        else()
            set(HOMEBREW_LLVM_PREFIX ${HOMEBREW_LLVM_PREFIX_X64})
        endif()
        
        message(STATUS "macOS 检测: 架构=${CMAKE_HOST_SYSTEM_PROCESSOR}, Homebrew LLVM 路径=${HOMEBREW_LLVM_PREFIX}")
    endif()
    
    # 尝试查找 libc++ 模块目录 (LLVM 19-21)
    find_path(LIBCXX_MODULE_DIRS
        NAMES std.cppm
        PATHS
            # Linux 路径
            /usr/lib/llvm-21/share/libc++/v1
            /usr/lib/llvm-20/share/libc++/v1
            /usr/lib/llvm-19/share/libc++/v1
            /usr/local/lib/llvm-21/share/libc++/v1
            /usr/local/lib/llvm-20/share/libc++/v1
            /usr/local/lib/llvm-19/share/libc++/v1
            /opt/llvm/share/libc++/v1
            # macOS Homebrew 路径 (Apple Silicon & Intel)
            /opt/homebrew/opt/llvm/share/libc++/v1
            /usr/local/opt/llvm/share/libc++/v1
        DOC "Path to libc++ standard library modules"
    )
    
    # 尝试查找 libc++ 头文件目录 (LLVM 19-21)
    find_path(LIBCXX_INCLUDE_DIRS
        NAMES __config
        PATHS
            # Linux 路径
            /usr/lib/llvm-21/include/c++/v1
            /usr/lib/llvm-20/include/c++/v1
            /usr/lib/llvm-19/include/c++/v1
            /usr/local/lib/llvm-21/include/c++/v1
            /usr/local/lib/llvm-20/include/c++/v1
            /usr/local/lib/llvm-19/include/c++/v1
            /opt/llvm/include/c++/v1
            /usr/include/c++/v1
            # macOS Homebrew 路径 (Apple Silicon & Intel)
            /opt/homebrew/opt/llvm/include/c++/v1
            /usr/local/opt/llvm/include/c++/v1
        DOC "Path to libc++ headers"
    )
    
    if(LIBCXX_MODULE_DIRS AND LIBCXX_INCLUDE_DIRS)
        message(STATUS "自动检测到 libc++ 路径:")
        message(STATUS "  模块目录: ${LIBCXX_MODULE_DIRS}")
        message(STATUS "  头文件目录: ${LIBCXX_INCLUDE_DIRS}")
        
        # 强制所有目标使用 libc++ (必须在添加第三方库之前设置)
        if(APPLE)
            # macOS: 需要指定 sysroot 和 LLVM libc++ 库路径
            # 获取 macOS SDK 路径
            execute_process(
                COMMAND xcrun --show-sdk-path
                OUTPUT_VARIABLE MACOS_SDK_PATH
                OUTPUT_STRIP_TRAILING_WHITESPACE
            )
            message(STATUS "macOS SDK 路径: ${MACOS_SDK_PATH}")
            
            # 从模块目录推断 LLVM 安装路径
            get_filename_component(LLVM_LIB_PATH "${LIBCXX_MODULE_DIRS}/../../../lib/c++" ABSOLUTE)
            message(STATUS "LLVM libc++ 库路径: ${LLVM_LIB_PATH}")
            
            add_compile_options(
                -nostdinc++
                -isystem ${LIBCXX_INCLUDE_DIRS}
                -isysroot ${MACOS_SDK_PATH}
            )
            # macOS 链接选项：指定 LLVM libc++ 路径避免与系统 libc++ 冲突
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
        message(WARNING "无法自动检测 libc++ 路径。请手动设置:")
        message(WARNING "  cmake -DLIBCXX_MODULE_DIRS=<path> -DLIBCXX_INCLUDE_DIRS=<path> ..")
    endif()
    
elseif(WIN32)
    # Windows: 自动检测 MSVC 标准库模块路径
    
    # 优先级1: 尝试从 VCToolsInstallDir 环境变量获取
    if(DEFINED ENV{VCToolsInstallDir})
        set(LIBCXX_MODULE_DIRS "$ENV{VCToolsInstallDir}/modules" CACHE PATH "Path to MSVC modules")
        if(EXISTS "${LIBCXX_MODULE_DIRS}/std.ixx")
            message(STATUS "从 VCToolsInstallDir 环境变量检测到 MSVC 模块目录: ${LIBCXX_MODULE_DIRS}")
        else()
            message(WARNING "找到 VCToolsInstallDir 但 std.ixx 不存在于: ${LIBCXX_MODULE_DIRS}")
            unset(LIBCXX_MODULE_DIRS CACHE)
        endif()
    endif()
    
    # 优先级2: 如果环境变量未找到或无效，尝试从编译器路径检测
    if(NOT LIBCXX_MODULE_DIRS)
        # 从编译器路径提取 MSVC 版本目录
        # cl.exe 位于: VC/Tools/MSVC/{version}/bin/Hostx64/x64/cl.exe
        # modules 位于: VC/Tools/MSVC/{version}/modules/
        # 所以需要从 cl.exe 目录向上3级
        get_filename_component(COMPILER_DIR "${CMAKE_CXX_COMPILER}" DIRECTORY)
        get_filename_component(COMPILER_HOST_DIR "${COMPILER_DIR}" DIRECTORY)
        get_filename_component(COMPILER_BIN_DIR "${COMPILER_HOST_DIR}" DIRECTORY)
        get_filename_component(MSVC_VERSION_DIR "${COMPILER_BIN_DIR}" DIRECTORY)
        
        # 首先尝试从编译器路径提取的路径
        set(DETECTED_MODULE_PATH "${MSVC_VERSION_DIR}/modules")
        
        if(EXISTS "${DETECTED_MODULE_PATH}/std.ixx")
            set(LIBCXX_MODULE_DIRS "${DETECTED_MODULE_PATH}" CACHE PATH "Path to MSVC modules")
            message(STATUS "从编译器路径自动检测到 MSVC 模块目录: ${LIBCXX_MODULE_DIRS}")
        else()
            # 后备方案: 在常见的 Visual Studio 安装位置搜索
            message(STATUS "无法从编译器路径检测，搜索常见安装位置...")
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
                message(STATUS "在常见位置找到 MSVC 模块目录: ${LIBCXX_MODULE_DIRS}")
            else()
                message(WARNING "无法自动检测 MSVC 模块路径。请手动设置:")
                message(WARNING "  cmake -DLIBCXX_MODULE_DIRS=<path> ..")
            endif()
        endif()
    endif()
endif()


# ==============================================================================
# 编译器标志
# ==============================================================================
if(MSVC)
    add_compile_options(/EHsc /W4 /utf-8 /Zc:__cplusplus /Zc:preprocessor)
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
# 第三方库配置
# ==============================================================================
# stdexec: 纯头文件模式（不走 add_subdirectory，避免 CPM 依赖）
# 用于上层 sender/receiver 组织，底层 I/O 使用原生协程
include_directories(SYSTEM ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/stdexec/include)

# ==============================================================================
# 平台特定 I/O 后端检测
# ==============================================================================
if(WIN32)
    set(CNETMOD_HAS_IOCP ON)
    message(STATUS "I/O 后端: IOCP (Windows)")
elseif(APPLE)
    set(CNETMOD_HAS_KQUEUE ON)
    message(STATUS "I/O 后端: kqueue (macOS)")
elseif(UNIX)
    # 检测 io_uring 支持
    include(CheckIncludeFileCXX)
    check_include_file_cxx("liburing.h" CNETMOD_HAS_IO_URING)
    if(CNETMOD_HAS_IO_URING)
        message(STATUS "I/O 后端: io_uring (Linux)")
    endif()
    # epoll 在 Linux 上始终可用
    set(CNETMOD_HAS_EPOLL ON)
    message(STATUS "I/O 后端: epoll (Linux)")
endif()

# ==============================================================================
# 源文件收集
# ==============================================================================

# 收集模块接口文件
file(GLOB_RECURSE MODULE_INTERFACE_FILES
    "src/*.cppm"
    "src/*.ixx"
)

# 收集实现文件（排除 main.cpp）
file(GLOB_RECURSE CPP_SOURCES "src/*.cpp")
list(REMOVE_ITEM CPP_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/main.cpp")

# 按平台过滤：排除当前平台不需要的 I/O 后端模块
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

# ==============================================================================
# 核心库目标: cnetmod_core
# ==============================================================================

add_library(cnetmod_core STATIC
    ${CPP_SOURCES}
    ${MODULE_INTERFACE_FILES}
)

target_include_directories(cnetmod_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)

# 平台特定宏定义
target_compile_definitions(cnetmod_core PUBLIC
    $<$<PLATFORM_ID:Windows>:CNETMOD_PLATFORM_WINDOWS>
    $<$<PLATFORM_ID:Linux>:CNETMOD_PLATFORM_LINUX>
    $<$<PLATFORM_ID:Darwin>:CNETMOD_PLATFORM_MACOS>
    $<$<BOOL:${CNETMOD_HAS_IOCP}>:CNETMOD_HAS_IOCP>
    $<$<BOOL:${CNETMOD_HAS_IO_URING}>:CNETMOD_HAS_IO_URING>
    $<$<BOOL:${CNETMOD_HAS_EPOLL}>:CNETMOD_HAS_EPOLL>
    $<$<BOOL:${CNETMOD_HAS_KQUEUE}>:CNETMOD_HAS_KQUEUE>
)

# 平台特定链接
if(WIN32)
    target_link_libraries(cnetmod_core PUBLIC ws2_32 mswsock)
elseif(CNETMOD_HAS_IO_URING)
    target_link_libraries(cnetmod_core PUBLIC uring)
endif()

# ==============================================================================
# C++ 模块配置
# ==============================================================================

if(UNIX AND LIBCXX_MODULE_DIRS AND LIBCXX_INCLUDE_DIRS)
    # Linux/macOS: libc++ 标准库模块
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
    )
elseif(WIN32 AND LIBCXX_MODULE_DIRS)
    # Windows: MSVC 标准库模块
    target_sources(cnetmod_core PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR} ${LIBCXX_MODULE_DIRS}
        FILES
            ${MODULE_INTERFACE_FILES}
            ${LIBCXX_MODULE_DIRS}/std.ixx
            ${LIBCXX_MODULE_DIRS}/std.compat.ixx
    )
else()
    message(WARNING "标准库模块路径未检测到，请手动设置 LIBCXX_MODULE_DIRS")
    target_sources(cnetmod_core PUBLIC
        FILE_SET cxx_modules TYPE CXX_MODULES
        BASE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}
        FILES ${MODULE_INTERFACE_FILES}
    )
endif()

# 模块生成模式
set_property(TARGET cnetmod_core PROPERTY
    CXX_MODULE_GENERATION_MODE "SEPARATE"
)

# ==============================================================================
# 主可执行文件
# ==============================================================================

add_executable(cnetmod_app src/main.cpp)
target_link_libraries(cnetmod_app PRIVATE cnetmod_core)
set_property(TARGET cnetmod_app PROPERTY
    CXX_MODULE_GENERATION_MODE "SEPARATE"
)

# ==============================================================================
# 测试配置
# ==============================================================================

option(CNETMOD_BUILD_TESTS "Build tests" OFF)
if(CNETMOD_BUILD_TESTS AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tests/CMakeLists.txt")
    add_subdirectory(tests)
endif()

# ==============================================================================
# 示例程序配置
# ==============================================================================

option(CNETMOD_BUILD_EXAMPLES "Build examples" ON)
if(CNETMOD_BUILD_EXAMPLES AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/examples/CMakeLists.txt")
    add_subdirectory(examples)
endif()

# ==============================================================================
# 构建信息汇总
# ==============================================================================

message(STATUS "==========================================")
message(STATUS "项目: ${PROJECT_NAME} v${PROJECT_VERSION}")
message(STATUS "C++ 标准: C++${CMAKE_CXX_STANDARD}")
message(STATUS "编译器: ${CMAKE_CXX_COMPILER_ID} ${CMAKE_CXX_COMPILER_VERSION}")
message(STATUS "构建类型: ${CMAKE_BUILD_TYPE}")
message(STATUS "平台: ${CMAKE_SYSTEM_NAME}")
if(LIBCXX_MODULE_DIRS)
    message(STATUS "标准库模块: ${LIBCXX_MODULE_DIRS}")
endif()
message(STATUS "==========================================")