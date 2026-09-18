include_guard(GLOBAL)

macro(cnetmod_configure_glaze)
    set(CNETMOD_GLAZE_SOURCE_DIR
        "${PROJECT_SOURCE_DIR}/3rdparty/glaze" CACHE PATH
        "Pinned Glaze source directory")
    if(NOT EXISTS "${CNETMOD_GLAZE_SOURCE_DIR}/include/glaze/glaze.hpp")
        message(FATAL_ERROR
            "Glaze is unavailable at ${CNETMOD_GLAZE_SOURCE_DIR}. "
            "Initialize the 3rdparty/glaze submodule.")
    endif()
    if(NOT TARGET cnetmod_glaze)
        add_library(cnetmod_glaze INTERFACE)
        add_library(glaze::glaze ALIAS cnetmod_glaze)
        set_property(TARGET cnetmod_glaze PROPERTY EXPORT_NAME glaze)
        target_compile_features(cnetmod_glaze INTERFACE cxx_std_23)
        target_include_directories(cnetmod_glaze INTERFACE
            "$<BUILD_INTERFACE:${CNETMOD_GLAZE_SOURCE_DIR}/include>"
            "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
    endif()
endmacro()

function(cnetmod_link_glaze TARGET_NAME)
    target_link_libraries(${TARGET_NAME} PUBLIC cnetmod_glaze)
endfunction()
