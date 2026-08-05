include_guard(GLOBAL)
include(${CMAKE_CURRENT_LIST_DIR}/DependencyHelpers.cmake)

macro(cnetmod_configure_jwt_cpp)
    set(CNETMOD_JWT_CPP_INCLUDE_DIR "" CACHE PATH "jwt-cpp include directory")
    if(CNETMOD_ENABLE_HTTP)
        if(CNETMOD_USE_SYSTEM_DEPS)
            find_package(jwt-cpp CONFIG QUIET)
            cnetmod_dependency_target_include(_jwt_from_target jwt-cpp::jwt-cpp jwt-cpp/jwt.h)
            if(_jwt_from_target)
                set(CNETMOD_JWT_CPP_INCLUDE_DIR "${_jwt_from_target}" CACHE PATH "" FORCE)
            endif()
        endif()
        if(NOT CNETMOD_JWT_CPP_INCLUDE_DIR AND EXISTS "${PROJECT_SOURCE_DIR}/3rdparty/jwt-cpp/include/jwt-cpp/jwt.h")
            set(CNETMOD_JWT_CPP_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/3rdparty/jwt-cpp/include" CACHE PATH "" FORCE)
        endif()
        set(CNETMOD_HAS_JWT_CPP OFF)
        if(EXISTS "${CNETMOD_JWT_CPP_INCLUDE_DIR}/jwt-cpp/jwt.h")
            set(CNETMOD_HAS_JWT_CPP ON)
        endif()
    endif()
endmacro()

function(cnetmod_link_jwt_cpp TARGET_NAME)
    if(CNETMOD_HAS_JWT_CPP)
        target_include_directories(${TARGET_NAME} SYSTEM PRIVATE "${CNETMOD_JWT_CPP_INCLUDE_DIR}")

        # jwt-cpp includes <openssl/...> in its public headers.  A consumer
        # which includes jwt-cpp directly (for example account_server_demo)
        # therefore needs BoringSSL's OpenSSL-compatible include directory,
        # not only the one used while compiling cnetmod_core.
        if(BoringSSL_FOUND AND DEFINED BoringSSL_INCLUDE_DIRS)
            # This must be a normal BEFORE include, not SYSTEM: Clang searches
            # all -I paths before -isystem paths. A Homebrew OpenSSL -I path
            # would otherwise win and generate incompatible SSL*_ctrl calls.
            target_include_directories(${TARGET_NAME} BEFORE PRIVATE
                ${BoringSSL_INCLUDE_DIRS})
        endif()
        if(BoringSSL_FOUND AND DEFINED BoringSSL_LIBRARIES)
            target_link_libraries(${TARGET_NAME} PRIVATE ${BoringSSL_LIBRARIES})
        endif()
    endif()
endfunction()
