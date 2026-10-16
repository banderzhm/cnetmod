include_guard(GLOBAL)

macro(cnetmod_configure_nlohmann_json)
    set(CNETMOD_NLOHMANN_JSON_SOURCE_DIR
        "${PROJECT_SOURCE_DIR}/3rdparty/json" CACHE PATH
        "Pinned nlohmann/json source directory")
    set(CNETMOD_NLOHMANN_JSON_MODULE
        "${CNETMOD_NLOHMANN_JSON_SOURCE_DIR}/src/modules/json.cppm" CACHE FILEPATH
        "Pinned nlohmann/json C++ module interface")

    if(NOT EXISTS "${CNETMOD_NLOHMANN_JSON_SOURCE_DIR}/include/nlohmann/json.hpp" OR
       NOT EXISTS "${CNETMOD_NLOHMANN_JSON_MODULE}")
        message(FATAL_ERROR
            "nlohmann/json is unavailable at ${CNETMOD_NLOHMANN_JSON_SOURCE_DIR}. "
            "Initialize the 3rdparty/json submodule.")
    endif()
endmacro()

function(cnetmod_link_nlohmann_json TARGET_NAME)
    target_include_directories(${TARGET_NAME} PUBLIC
        $<BUILD_INTERFACE:${CNETMOD_NLOHMANN_JSON_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
endfunction()
