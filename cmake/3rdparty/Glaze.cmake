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
    # Glaze is an implementation detail of src/json/json.cpp. Its headers and
    # compiler contract must not leak through cnetmod's public usage requirements.
    target_include_directories(${TARGET_NAME} PRIVATE
        "${CNETMOD_GLAZE_SOURCE_DIR}/include")
    target_compile_options(${TARGET_NAME} PRIVATE
        $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/Zc:preprocessor>
        $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/permissive->
        $<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/Zc:lambda>)
endfunction()
