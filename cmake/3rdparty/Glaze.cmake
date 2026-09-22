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
endmacro()

function(cnetmod_link_glaze TARGET_NAME)
    # cnetmod.json exports direct Glaze templates and native document types.
    target_include_directories(${TARGET_NAME} PUBLIC
        $<BUILD_INTERFACE:${CNETMOD_GLAZE_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
    target_compile_options(${TARGET_NAME} PUBLIC
        $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/Zc:preprocessor>
        $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/permissive->
        $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/Zc:lambda>)
endfunction()
