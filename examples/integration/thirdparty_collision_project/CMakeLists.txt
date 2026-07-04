cmake_minimum_required(VERSION 3.28)

project(cnetmod_thirdparty_collision_project
    VERSION 0.1.0
    LANGUAGES C CXX
)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CXX_SCAN_FOR_MODULES ON)

if(NOT DEFINED CNETMOD_SOURCE_DIR)
    get_filename_component(CNETMOD_SOURCE_DIR
        "${CMAKE_CURRENT_LIST_DIR}/../../.."
        ABSOLUTE
    )
endif()

message(STATUS "Using cnetmod source: ${CNETMOD_SOURCE_DIR}")

# This project intentionally owns the same dependencies that cnetmod can use.
# The correct integration pattern is:
#   1. The host project creates dependency targets first.
#   2. cnetmod sees those targets and reuses them instead of adding its own
#      subdirectories again.
#   3. Header-only paths are passed to cnetmod through cache variables.

# Host-owned stdexec / jwt-cpp / nlohmann_json checkouts.
set(CNETMOD_STDEXEC_INCLUDE_DIR
    "${CNETMOD_SOURCE_DIR}/3rdparty/stdexec/include"
    CACHE PATH "" FORCE
)
set(CNETMOD_JWT_CPP_INCLUDE_DIR
    "${CNETMOD_SOURCE_DIR}/3rdparty/jwt-cpp/include"
    CACHE PATH "" FORCE
)
set(CNETMOD_JSON_INCLUDE_DIR
    "${CNETMOD_SOURCE_DIR}/3rdparty/json/include"
    CACHE PATH "" FORCE
)
set(CNETMOD_JSON_MODULE
    "${CNETMOD_SOURCE_DIR}/3rdparty/json/src/modules/json.cppm"
    CACHE FILEPATH "" FORCE
)

# Host-owned pugixml target. cnetmod should reuse pugixml::pugixml.
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/host_deps/pugixml")
add_subdirectory(
    "${CNETMOD_SOURCE_DIR}/3rdparty/pugixml"
    "${CMAKE_CURRENT_BINARY_DIR}/host_deps/pugixml"
    EXCLUDE_FROM_ALL
)

# Host-owned nghttp2 target. cnetmod should reuse nghttp2_static.
set(ENABLE_LIB_ONLY ON CACHE BOOL "" FORCE)
set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
set(ENABLE_DOC OFF CACHE BOOL "" FORCE)
set(ENABLE_FAILMALLOC OFF CACHE BOOL "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
set(CNETMOD_HOST_NGHTTP2_BINARY_DIR "${CMAKE_CURRENT_BINARY_DIR}/host_deps/nghttp2")
file(MAKE_DIRECTORY
    "${CNETMOD_HOST_NGHTTP2_BINARY_DIR}/lib"
    "${CNETMOD_HOST_NGHTTP2_BINARY_DIR}/lib/includes/nghttp2"
    "${CNETMOD_HOST_NGHTTP2_BINARY_DIR}/integration-tests"
    "${CNETMOD_HOST_NGHTTP2_BINARY_DIR}/doc"
)
add_subdirectory(
    "${CNETMOD_SOURCE_DIR}/3rdparty/nghttp2"
    "${CNETMOD_HOST_NGHTTP2_BINARY_DIR}"
    EXCLUDE_FROM_ALL
)

# Host-owned LevelDB target. cnetmod should reuse leveldb.
set(LEVELDB_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(LEVELDB_BUILD_BENCHMARKS OFF CACHE BOOL "" FORCE)
set(LEVELDB_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory(
    "${CNETMOD_SOURCE_DIR}/3rdparty/leveldb"
    "${CMAKE_CURRENT_BINARY_DIR}/host_deps/leveldb"
    EXCLUDE_FROM_ALL
)

# Now add cnetmod. Its CMake should not duplicate any of the targets above.
set(CNETMOD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(CNETMOD_BUILD_BENCH OFF CACHE BOOL "" FORCE)
set(CNETMOD_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
add_subdirectory(
    "${CNETMOD_SOURCE_DIR}"
    "${CMAKE_CURRENT_BINARY_DIR}/host_deps/cnetmod"
    EXCLUDE_FROM_ALL
)

add_executable(thirdparty_collision_app main.cpp)
if(NOT CNETMOD_STDEXEC_EFFECTIVE_INCLUDE_DIR)
    set(CNETMOD_STDEXEC_EFFECTIVE_INCLUDE_DIR "${CNETMOD_STDEXEC_INCLUDE_DIR}")
endif()
target_include_directories(thirdparty_collision_app
    PRIVATE
        ${CNETMOD_STDEXEC_EFFECTIVE_INCLUDE_DIR}
)
target_link_libraries(thirdparty_collision_app
    PRIVATE
        cnetmod_core
        pugixml::pugixml
        leveldb
)

set_property(TARGET thirdparty_collision_app PROPERTY
    CXX_MODULE_GENERATION_MODE "SEPARATE"
)

if(MSVC)
    target_compile_options(thirdparty_collision_app PRIVATE
        /std:c++latest
        /bigobj
        /utf-8
        /EHsc
        /Zc:__cplusplus
        /Zc:preprocessor
    )
endif()
