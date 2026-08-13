include_guard(GLOBAL)

macro(cnetmod_configure_brotli)
    set(CNETMOD_BROTLI_ROOT "" CACHE PATH "Optional Brotli installation prefix")
    set(CNETMOD_HAS_BROTLI OFF)
    if(CNETMOD_ENABLE_GRPC)
        find_package(unofficial-brotli CONFIG QUIET)
        if(TARGET unofficial::brotli::brotlienc AND TARGET unofficial::brotli::brotlidec)
            set(CNETMOD_BROTLI_TARGETS unofficial::brotli::brotlienc unofficial::brotli::brotlidec)
            set(CNETMOD_HAS_BROTLI ON)
        else()
            find_path(CNETMOD_BROTLI_INCLUDE_DIR brotli/encode.h
                HINTS "${CNETMOD_BROTLI_ROOT}" PATH_SUFFIXES include)
            find_library(CNETMOD_BROTLI_ENC_LIBRARY NAMES brotlienc
                HINTS "${CNETMOD_BROTLI_ROOT}" PATH_SUFFIXES lib lib64)
            find_library(CNETMOD_BROTLI_DEC_LIBRARY NAMES brotlidec
                HINTS "${CNETMOD_BROTLI_ROOT}" PATH_SUFFIXES lib lib64)
            if(CNETMOD_BROTLI_INCLUDE_DIR AND CNETMOD_BROTLI_ENC_LIBRARY AND
                CNETMOD_BROTLI_DEC_LIBRARY)
                add_library(cnetmod_brotlienc UNKNOWN IMPORTED)
                set_target_properties(cnetmod_brotlienc PROPERTIES
                    IMPORTED_LOCATION "${CNETMOD_BROTLI_ENC_LIBRARY}"
                    INTERFACE_INCLUDE_DIRECTORIES "${CNETMOD_BROTLI_INCLUDE_DIR}")
                add_library(cnetmod_brotlidec UNKNOWN IMPORTED)
                set_target_properties(cnetmod_brotlidec PROPERTIES
                    IMPORTED_LOCATION "${CNETMOD_BROTLI_DEC_LIBRARY}"
                    INTERFACE_INCLUDE_DIRECTORIES "${CNETMOD_BROTLI_INCLUDE_DIR}")
                set(CNETMOD_BROTLI_TARGETS cnetmod_brotlienc cnetmod_brotlidec)
                set(CNETMOD_HAS_BROTLI ON)
            endif()
        endif()
    endif()
endmacro()

function(cnetmod_link_brotli TARGET_NAME)
    if(CNETMOD_HAS_BROTLI)
        target_link_libraries(${TARGET_NAME} PUBLIC ${CNETMOD_BROTLI_TARGETS})
    endif()
endfunction()
